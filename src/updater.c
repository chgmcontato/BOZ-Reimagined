/*
 * Automatic updates from GitHub releases (see updater.h).
 *
 *   1. github.com/<repo>/releases/latest redirects to .../releases/tag/<tag> (drafts and
 *      pre-releases are never "latest"). The redirect, not api.github.com, because the API
 *      allows 60 anonymous requests an hour per IP, which a shared IP (CGNAT) runs out of.
 *   2. When the tag is newer than CODBOZ_VERSION: download SHA256SUMS.txt and this
 *      platform's archive from github.com/<repo>/releases/download/<tag>/, check the hash.
 *   3. Take the executable out of the archive and put it in place of the running one:
 *      Linux renames <exe>.new over <exe> (the running image stays valid); Windows cannot
 *      overwrite a running .exe but can rename it, so <exe> → <exe>.old, <exe>.new → <exe>,
 *      and <exe>.old is deleted on the next start.
 *
 * HTTPS goes through WinHTTP on Windows and the curl command on Linux, so the executable
 * needs no TLS library. HTTPS to github.com authenticates the release; SHA256SUMS.txt
 * catches a truncated or corrupted download.
 */
#include "updater.h"
#include "log.h"

#include "../third_party/stb_image.h"

#include <SDL.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#else
#include <unistd.h>
#endif

#ifndef CODBOZ_UPDATE_REPO
#define CODBOZ_UPDATE_REPO "chgmcontato/BOZ-Reimagined"
#endif

#ifdef _WIN32
#define ASSET_NAME "codboz-pc-windows-x64.zip"
#define ASSET_ZIP true
#define EXE_NAME "codboz-pc.exe"
#else
#define ASSET_NAME "codboz-pc-linux-x86_64.tar.gz"
#define ASSET_ZIP false
#define EXE_NAME "codboz-pc"
#endif

enum { MAX_DOWNLOAD = 64 << 20, MAX_EXE = 256 << 20 };

/* ---- shared state (written by the update thread, read by the launcher) ---- */

static int g_state = UPDATER_OFF;
static int g_running, g_enabled;
static char g_new_version[32];
static char g_error[256];
static char g_exe[1024];
static int g_argc;
static char **g_argv;

static void set_state(enum updater_state s) {
    __atomic_store_n(&g_state, (int)s, __ATOMIC_RELEASE);
}

enum updater_state updater_state(void) {
    return (enum updater_state)__atomic_load_n(&g_state, __ATOMIC_ACQUIRE);
}

const char *updater_version(void) {
#ifdef CODBOZ_VERSION
    return CODBOZ_VERSION[0] ? CODBOZ_VERSION : "dev";
#else
    return "dev";
#endif
}

const char *updater_new_version(void) {
    return g_new_version;
}

const char *updater_error(void) {
    return g_error;
}

/* ---- versions ---- */

int updater_compare_versions(const char *a, const char *b) {
    if (*a == 'v' || *a == 'V') {
        a++;
    }
    if (*b == 'v' || *b == 'V') {
        b++;
    }
    for (int part = 0; part < 4; ++part) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) {
            return x < y ? -1 : 1;
        }
        if (*a == '.') {
            a++;
        }
        if (*b == '.') {
            b++;
        }
        if (!isdigit((unsigned char)*a) && !isdigit((unsigned char)*b)) {
            break;
        }
    }
    return 0;
}

/* Tags end up in URLs and file names: only plain version characters. */
static bool valid_tag(const char *tag) {
    size_t n = strlen(tag);
    if (n == 0 || n >= sizeof(g_new_version)) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)tag[i];
        if (!isalnum(c) && c != '.' && c != '-' && c != '_') {
            return false;
        }
    }
    return true;
}

/* ---- SHA-256 ---- */

static const uint32_t k_sha[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_block(uint32_t h[8], const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 |
               p[4 * i + 3];
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + k_sha[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

void updater_sha256_hex(const void *data, size_t size, char hex[65]) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const uint8_t *p = data;
    size_t left = size;
    for (; left >= 64; left -= 64, p += 64) {
        sha_block(h, p);
    }
    uint8_t tail[128] = {0};
    memcpy(tail, p, left);
    tail[left] = 0x80;
    size_t tail_len = left < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)size * 8;
    for (int i = 0; i < 8; ++i) {
        tail[tail_len - 1 - i] = (uint8_t)(bits >> (8 * i));
    }
    sha_block(h, tail);
    if (tail_len == 128) {
        sha_block(h, tail + 64);
    }
    for (int i = 0; i < 8; ++i) {
        snprintf(hex + 8 * i, 9, "%08x", h[i]);
    }
}

bool updater_find_sum(const char *sums, const char *file, char hex[65]) {
    size_t file_len = strlen(file);
    for (const char *line = sums; line && *line;) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        /* "<64 hex>  <name>" (sha256sum text mode) or "<64 hex> *<name>" (binary mode) */
        if (len >= 66 + file_len) {
            const char *name = line + 64;
            while (name < line + len && (*name == ' ' || *name == '*')) {
                name++;
            }
            size_t name_len = (size_t)(line + len - name);
            while (name_len && (name[name_len - 1] == '\r' || name[name_len - 1] == ' ')) {
                name_len--;
            }
            bool all_hex = true;
            for (int i = 0; i < 64; ++i) {
                all_hex &= isxdigit((unsigned char)line[i]) != 0;
            }
            if (all_hex && name_len == file_len && memcmp(name, file, file_len) == 0) {
                for (int i = 0; i < 64; ++i) {
                    hex[i] = (char)tolower((unsigned char)line[i]);
                }
                hex[64] = 0;
                return true;
            }
        }
        line = end ? end + 1 : NULL;
    }
    return false;
}

/* ---- archives ---- */

static uint32_t le16(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t le32(const uint8_t *p) {
    return le16(p) | le16(p + 2) << 16;
}

static bool basename_is(const char *path, size_t len, const char *name) {
    size_t name_len = strlen(name);
    if (len < name_len || memcmp(path + len - name_len, name, name_len) != 0) {
        return false;
    }
    return len == name_len || path[len - name_len - 1] == '/' || path[len - name_len - 1] == '\\';
}

static uint8_t *inflate_raw(const uint8_t *src, size_t src_len, size_t out_len) {
    if (out_len == 0 || out_len > MAX_EXE || src_len > MAX_DOWNLOAD) {
        return NULL;
    }
    uint8_t *out = malloc(out_len);
    if (out && stbi_zlib_decode_noheader_buffer((char *)out, (int)out_len, (const char *)src,
                                                (int)src_len) != (int)out_len) {
        free(out);
        out = NULL;
    }
    return out;
}

static uint8_t *zip_extract(const uint8_t *z, size_t size, const char *name, size_t *out_size) {
    if (size < 22) {
        return NULL;
    }
    size_t eocd = 0;
    bool found = false;
    for (size_t i = size - 22 + 1; i-- > 0 && size - i < 22 + 65536;) {
        if (le32(z + i) == 0x06054b50u) {
            eocd = i;
            found = true;
            break;
        }
    }
    if (!found) {
        return NULL;
    }
    uint32_t count = le16(z + eocd + 10), cd_size = le32(z + eocd + 12), pos = le32(z + eocd + 16);
    if ((size_t)pos + cd_size > eocd) {
        return NULL;
    }
    for (uint32_t i = 0; i < count && pos + 46 <= eocd; ++i) {
        const uint8_t *h = z + pos;
        if (le32(h) != 0x02014b50u) {
            return NULL;
        }
        uint32_t method = le16(h + 10), comp = le32(h + 20), full = le32(h + 24);
        uint32_t name_len = le16(h + 28), local = le32(h + 42);
        if (pos + 46 + name_len > eocd) {
            return NULL;
        }
        if (basename_is((const char *)h + 46, name_len, name)) {
            if ((size_t)local + 30 > size || le32(z + local) != 0x04034b50u) {
                return NULL;
            }
            size_t data = (size_t)local + 30 + le16(z + local + 26) + le16(z + local + 28);
            if (data + comp > size || full == 0 || full > MAX_EXE) {
                return NULL;
            }
            uint8_t *out = NULL;
            if (method == 0 && comp == full) {
                out = malloc(full);
                if (out) {
                    memcpy(out, z + data, full);
                }
            } else if (method == 8) {
                out = inflate_raw(z + data, comp, full);
            }
            if (out) {
                *out_size = full;
            }
            return out;
        }
        pos += 46 + name_len + le16(h + 30) + le16(h + 32);
    }
    return NULL;
}

static uint8_t *targz_extract(const uint8_t *g, size_t size, const char *name, size_t *out_size) {
    /* gzip: 10-byte header, optional fields, raw deflate, CRC32, ISIZE */
    if (size < 18 || g[0] != 0x1f || g[1] != 0x8b || g[2] != 8) {
        return NULL;
    }
    uint8_t flags = g[3];
    size_t pos = 10;
    if (flags & 4) {
        pos += 2 + (size_t)le16(g + pos);
    }
    for (int bit = 8; bit <= 16; bit <<= 1) {
        if (flags & bit) {
            while (pos < size && g[pos]) {
                pos++;
            }
            pos++;
        }
    }
    if (flags & 2) {
        pos += 2;
    }
    if (pos + 8 > size) {
        return NULL;
    }
    size_t tar_size = le32(g + size - 4);
    uint8_t *tar = inflate_raw(g + pos, size - 8 - pos, tar_size);
    if (!tar) {
        return NULL;
    }
    uint8_t *out = NULL;
    for (size_t off = 0; off + 512 <= tar_size;) {
        const uint8_t *h = tar + off;
        if (!h[0]) {
            break; /* end-of-archive blocks */
        }
        char path[260];
        size_t prefix = strnlen((const char *)h + 345, 155), base = strnlen((const char *)h, 100);
        if (memcmp(h + 257, "ustar", 5) == 0 && prefix) {
            snprintf(path, sizeof(path), "%.*s/%.*s", (int)prefix, (const char *)h + 345, (int)base,
                     (const char *)h);
        } else {
            snprintf(path, sizeof(path), "%.*s", (int)base, (const char *)h);
        }
        char octal[13];
        memcpy(octal, h + 124, 12);
        octal[12] = 0;
        size_t file_size = (size_t)strtoull(octal, NULL, 8);
        size_t data = off + 512;
        if (data + file_size > tar_size) {
            break;
        }
        char type = (char)h[156];
        if ((type == '0' || type == 0) && basename_is(path, strlen(path), name) && file_size &&
            file_size <= MAX_EXE) {
            out = malloc(file_size);
            if (out) {
                memcpy(out, tar + data, file_size);
                *out_size = file_size;
            }
            break;
        }
        off = data + (file_size + 511) / 512 * 512;
    }
    free(tar);
    return out;
}

uint8_t *updater_extract(const uint8_t *archive, size_t size, bool zip, const char *name,
                         size_t *out_size) {
    return zip ? zip_extract(archive, size, name, out_size)
               : targz_extract(archive, size, name, out_size);
}

/* ---- HTTPS ---- */

#ifdef _WIN32
static bool utf8_to_wide(const char *s, wchar_t *out, int count) {
    return MultiByteToWideChar(CP_UTF8, 0, s, -1, out, count) > 0;
}

/* GET url into a malloc'd buffer. With `location`, redirects are not followed: a 3xx answer
 * returns an empty body and its Location header there. */
static uint8_t *http_get(const char *url, size_t *out_size, char *location, size_t location_size,
                         char *err, size_t err_size) {
    wchar_t wurl[1024], host[256], path[2048];
    if (!utf8_to_wide(url, wurl, 1024)) {
        snprintf(err, err_size, "bad URL");
        return NULL;
    }
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) {
        snprintf(err, err_size, "bad URL");
        return NULL;
    }
    HINTERNET session = WinHttpOpen(L"codboz-pc-updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { /* before Windows 8.1 */
        session = WinHttpOpen(L"codboz-pc-updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    HINTERNET connect = NULL, request = NULL;
    uint8_t *buf = NULL;
    size_t len = 0;
    bool ok = false;
    DWORD status = 0, status_len = sizeof(status);
    if (!session) {
        goto done;
    }
    WinHttpSetTimeouts(session, 15000, 15000, 30000, 60000);
    connect = WinHttpConnect(session, host, uc.nPort, 0);
    request = connect ? WinHttpOpenRequest(connect, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                      : NULL;
    if (request && location) {
        DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof(never));
    }
    if (!request || !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL) ||
        !WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_len,
                             WINHTTP_NO_HEADER_INDEX)) {
        snprintf(err, err_size, "connection failed (WinHTTP error %lu)", GetLastError());
        goto done;
    }
    if (location && status >= 300 && status < 400) {
        wchar_t wloc[1024];
        DWORD loc_len = sizeof(wloc);
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, wloc,
                                 &loc_len, WINHTTP_NO_HEADER_INDEX) ||
            WideCharToMultiByte(CP_UTF8, 0, wloc, -1, location, (int)location_size, NULL, NULL) <= 0) {
            snprintf(err, err_size, "redirect without a location");
            goto done;
        }
        ok = true;
        goto done;
    }
    if (status != 200) {
        snprintf(err, err_size, "HTTP %lu", status);
        goto done;
    }
    for (;;) {
        DWORD avail = 0, got = 0;
        if (!WinHttpQueryDataAvailable(request, &avail)) {
            snprintf(err, err_size, "download interrupted");
            goto done;
        }
        if (avail == 0) {
            break;
        }
        if (len + avail > MAX_DOWNLOAD) {
            snprintf(err, err_size, "download too large");
            goto done;
        }
        uint8_t *grown = realloc(buf, len + avail + 1);
        if (!grown) {
            goto done;
        }
        buf = grown;
        if (!WinHttpReadData(request, buf + len, avail, &got)) {
            snprintf(err, err_size, "download interrupted");
            goto done;
        }
        len += got;
    }
    ok = true;
done:
    if (request) {
        WinHttpCloseHandle(request);
    }
    if (connect) {
        WinHttpCloseHandle(connect);
    }
    if (session) {
        WinHttpCloseHandle(session);
    }
    if (!ok) {
        if (!err[0]) {
            snprintf(err, err_size, "connection failed");
        }
        free(buf);
        return NULL;
    }
    if (!buf) {
        buf = malloc(1);
    }
    buf[len] = 0;
    *out_size = len;
    return buf;
}
#else
/* The curl command (installed on practically every desktop Linux); the URL is built from
 * a fixed prefix and a validated tag, so it is safe inside single quotes. */
/* GET url into a malloc'd buffer. With `location`, redirects are not followed: a 3xx answer
 * returns an empty body and its Location header there. */
static uint8_t *http_get(const char *url, size_t *out_size, char *location, size_t location_size,
                         char *err, size_t err_size) {
    if (strncmp(url, "https://", 8) != 0 || strchr(url, '\'')) {
        snprintf(err, err_size, "bad URL");
        return NULL;
    }
    char cmd[1400];
    if (location) { /* print where it redirects to instead of following it */
        snprintf(cmd, sizeof(cmd),
                 "curl -fsS --proto =https --connect-timeout 15 --max-time 60 -A codboz-pc-updater "
                 "-o /dev/null -w '%%{redirect_url}' '%s'",
                 url);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "curl -fsSL --proto =https --proto-redir =https --connect-timeout 15 --max-time 600 "
                 "-A codboz-pc-updater '%s'",
                 url);
    }
    FILE *p = popen(cmd, "r");
    if (!p) {
        snprintf(err, err_size, "cannot run curl");
        return NULL;
    }
    size_t len = 0, cap = 1 << 16;
    uint8_t *buf = malloc(cap + 1);
    bool too_big = false;
    while (buf) {
        if (len == cap) {
            if (cap >= MAX_DOWNLOAD) {
                too_big = true;
                break;
            }
            cap *= 2;
            uint8_t *grown = realloc(buf, cap + 1);
            if (!grown) {
                free(buf);
                buf = NULL;
                break;
            }
            buf = grown;
        }
        size_t got = fread(buf + len, 1, cap - len, p);
        if (got == 0) {
            break;
        }
        len += got;
    }
    int status = pclose(p);
    if (!buf || too_big || status != 0) {
        snprintf(err, err_size, too_big ? "download too large"
                                : status == 127 << 8 ? "curl is not installed"
                                                     : "download failed (curl status %d)",
                 status >> 8);
        free(buf);
        return NULL;
    }
    buf[len] = 0;
    *out_size = len;
    if (location) {
        snprintf(location, location_size, "%s", (const char *)buf);
        len = *out_size = 0;
        buf[0] = 0;
    }
    return buf;
}
#endif

/* ---- replacing the executable ---- */

bool updater_replace_exe(const char *exe_path, const uint8_t *data, size_t size, char *err,
                         size_t err_size) {
    char fresh[1100];
    snprintf(fresh, sizeof(fresh), "%s.new", exe_path);
#ifdef _WIN32
    char old[1100];
    snprintf(old, sizeof(old), "%s.old", exe_path);
    wchar_t wexe[1100], wfresh[1100], wold[1100];
    if (!utf8_to_wide(exe_path, wexe, 1100) || !utf8_to_wide(fresh, wfresh, 1100) ||
        !utf8_to_wide(old, wold, 1100)) {
        snprintf(err, err_size, "bad path");
        return false;
    }
    FILE *f = _wfopen(wfresh, L"wb");
#else
    FILE *f = fopen(fresh, "wb");
#endif
    if (!f) {
        snprintf(err, err_size, "cannot write next to the executable (%s)", strerror(errno));
        return false;
    }
    bool written = fwrite(data, 1, size, f) == size;
    written &= fclose(f) == 0;
    if (!written) {
        snprintf(err, err_size, "cannot write the new executable (disk full?)");
#ifdef _WIN32
        DeleteFileW(wfresh);
#else
        remove(fresh);
#endif
        return false;
    }
#ifdef _WIN32
    DeleteFileW(wold);
    if (!MoveFileExW(wexe, wold, MOVEFILE_REPLACE_EXISTING)) {
        snprintf(err, err_size, "cannot rename the running executable (error %lu)", GetLastError());
        DeleteFileW(wfresh);
        return false;
    }
    if (!MoveFileExW(wfresh, wexe, 0)) {
        snprintf(err, err_size, "cannot move the new executable in place (error %lu)", GetLastError());
        MoveFileExW(wold, wexe, 0);
        DeleteFileW(wfresh);
        return false;
    }
#else
    struct stat st;
    mode_t mode = stat(exe_path, &st) == 0 ? (st.st_mode & 07777) | 0700 : 0755;
    if (chmod(fresh, mode) != 0 || rename(fresh, exe_path) != 0) {
        snprintf(err, err_size, "cannot move the new executable in place (%s)", strerror(errno));
        remove(fresh);
        return false;
    }
#endif
    return true;
}

/* ---- the update ---- */

bool updater_run(const char *current, const char *exe_path, char *new_version,
                 size_t new_version_size, char *err, size_t err_size) {
    new_version[0] = 0;
    err[0] = 0;
    size_t size = 0;
    char where[1024] = "";
    uint8_t *none = http_get("https://github.com/" CODBOZ_UPDATE_REPO "/releases/latest", &size, where,
                             sizeof(where), err, err_size);
    if (!none) {
        return false;
    }
    free(none);
    /* https://github.com/<repo>/releases/tag/<tag> */
    const char *mark = strstr(where, "/releases/tag/");
    char tag[64] = "";
    if (mark) {
        snprintf(tag, sizeof(tag), "%s", mark + strlen("/releases/tag/"));
        tag[strcspn(tag, "?#\r\n")] = 0;
    }
    if (!valid_tag(tag)) {
        snprintf(err, err_size, "unexpected answer from GitHub");
        return false;
    }
    if (updater_compare_versions(tag, current) <= 0) {
        return true;
    }
    snprintf(new_version, new_version_size, "%s", tag);
    snprintf(g_new_version, sizeof(g_new_version), "%s", tag);
    set_state(UPDATER_DOWNLOADING);
    log_info("update: %s -> %s", current, tag);

    char base[256], url[400];
    snprintf(base, sizeof(base), "https://github.com/" CODBOZ_UPDATE_REPO "/releases/download/%s/",
             tag);
    snprintf(url, sizeof(url), "%sSHA256SUMS.txt", base);
    char *sums = (char *)http_get(url, &size, NULL, 0, err, err_size);
    char want[65];
    bool listed = sums && updater_find_sum(sums, ASSET_NAME, want);
    free(sums);
    if (!listed) {
        if (!err[0]) {
            snprintf(err, err_size, "%s is not listed in SHA256SUMS.txt", ASSET_NAME);
        }
        return false;
    }
    snprintf(url, sizeof(url), "%s%s", base, ASSET_NAME);
    uint8_t *archive = http_get(url, &size, NULL, 0, err, err_size);
    if (!archive) {
        return false;
    }
    char got[65];
    updater_sha256_hex(archive, size, got);
    if (strcmp(got, want) != 0) {
        free(archive);
        snprintf(err, err_size, "checksum mismatch for %s", ASSET_NAME);
        return false;
    }
    size_t exe_size = 0;
    uint8_t *exe = updater_extract(archive, size, ASSET_ZIP, EXE_NAME, &exe_size);
    free(archive);
    if (!exe) {
        snprintf(err, err_size, "%s not found in %s", EXE_NAME, ASSET_NAME);
        return false;
    }
    if (!__atomic_load_n(&g_enabled, __ATOMIC_ACQUIRE) && g_exe[0]) {
        free(exe);
        snprintf(err, err_size, "turned off in Settings");
        return false;
    }
    bool ok = updater_replace_exe(exe_path, exe, exe_size, err, err_size);
    free(exe);
    return ok;
}

static int update_thread(void *unused) {
    (void)unused;
    char version[32], err[256];
    bool ok = updater_run(updater_version(), g_exe, version, sizeof(version), err, sizeof(err));
    snprintf(g_error, sizeof(g_error), "%s", err);
    if (!version[0]) {
        if (!ok) {
            log_info("update check skipped: %s", err);
        }
        set_state(UPDATER_CURRENT);
    } else if (ok) {
        log_info("update: %s installed; it starts next time", version);
        set_state(UPDATER_READY);
    } else {
        log_error("update to %s failed: %s", version, err);
        set_state(__atomic_load_n(&g_enabled, __ATOMIC_ACQUIRE) ? UPDATER_FAILED : UPDATER_OFF);
    }
    __atomic_store_n(&g_running, 0, __ATOMIC_RELEASE);
    return 0;
}

static bool find_exe(char *out, size_t size) {
#ifdef _WIN32
    wchar_t w[1024];
    DWORD n = GetModuleFileNameW(NULL, w, 1024);
    return n > 0 && n < 1024 && WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)size, NULL, NULL) > 0;
#else
    ssize_t n = readlink("/proc/self/exe", out, size - 1);
    if (n <= 0) {
        return false;
    }
    out[n] = 0;
    return true;
#endif
}

static void start_thread(void) {
    if (strcmp(updater_version(), "dev") == 0 || !g_exe[0] ||
        __atomic_exchange_n(&g_running, 1, __ATOMIC_ACQ_REL)) {
        return;
    }
    set_state(UPDATER_CHECKING);
    SDL_Thread *t = SDL_CreateThread(update_thread, "updater", NULL);
    if (t) {
        SDL_DetachThread(t);
    } else {
        __atomic_store_n(&g_running, 0, __ATOMIC_RELEASE);
        set_state(UPDATER_OFF);
    }
}

void updater_init(int argc, char **argv, bool enabled) {
    g_argc = argc;
    g_argv = argv;
    if (!find_exe(g_exe, sizeof(g_exe))) {
        g_exe[0] = 0;
        return;
    }
    char leftover[1100];
    snprintf(leftover, sizeof(leftover), "%s.new", g_exe);
    remove(leftover);
#ifdef _WIN32
    wchar_t w[1100];
    snprintf(leftover, sizeof(leftover), "%s.old", g_exe);
    if (utf8_to_wide(leftover, w, 1100)) {
        DeleteFileW(w); /* the previous version, now that it no longer runs */
    }
#endif
    if (getenv("CODBOZ_NO_UPDATE")) {
        return;
    }
    __atomic_store_n(&g_enabled, enabled ? 1 : 0, __ATOMIC_RELEASE);
    if (enabled) {
        start_thread();
    }
}

void updater_set_enabled(bool enabled) {
    if (getenv("CODBOZ_NO_UPDATE")) {
        return;
    }
    __atomic_store_n(&g_enabled, enabled ? 1 : 0, __ATOMIC_RELEASE);
    enum updater_state s = updater_state();
    if (enabled && (s == UPDATER_OFF || s == UPDATER_FAILED)) {
        start_thread();
    } else if (!enabled && s != UPDATER_DOWNLOADING && s != UPDATER_READY &&
               !__atomic_load_n(&g_running, __ATOMIC_ACQUIRE)) {
        set_state(UPDATER_OFF);
    }
}

void updater_restart(void) {
    if (updater_state() != UPDATER_READY) {
        return;
    }
#ifdef _WIN32
    wchar_t exe[1100];
    if (!utf8_to_wide(g_exe, exe, 1100)) {
        return;
    }
    /* same command line; CreateProcessW may write into it, so pass a copy */
    const wchar_t *line = GetCommandLineW();
    size_t n = wcslen(line) + 1;
    wchar_t *copy = malloc(n * sizeof(wchar_t));
    if (!copy) {
        return;
    }
    memcpy(copy, line, n * sizeof(wchar_t));
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    BOOL ok = CreateProcessW(exe, copy, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    free(copy);
    if (!ok) {
        log_error("update: cannot start the new version (error %lu)", GetLastError());
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    SDL_Quit();
    exit(0);
#else
    /* exec in place: same process, same terminal; the window closes first */
    if (access(g_exe, X_OK) != 0) {
        log_error("update: cannot start the new version (%s)", strerror(errno));
        return;
    }
    char *fallback[] = {g_exe, NULL};
    char **args = g_argv && g_argc > 0 ? g_argv : fallback;
    fflush(NULL);
    SDL_Quit();
    execv(g_exe, args);
    log_error("update: cannot start the new version (%s); open the game again", strerror(errno));
    exit(1);
#endif
}
