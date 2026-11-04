/*
 * Installs a game folder from an APK the player owns:
 *   <root>/original/assets     from the Android APK 1.0.11 (com.activision.boz)
 *   <root>/reimagined/assets   from boz-reimagined's BOZ-TV.apk (same boz.s3e as the TV)
 *   <root>/ios/assets          from the iOS IPA 1.3.5 (game image, gles1 archive, music)
 * The Android OBB is never copied; its path is remembered in <root>/launcher.cfg.
 */
#include "install.h"
#include "log.h"

#include "../third_party/LzmaDec.h"
#include "../third_party/stb_image.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir_one(p) _mkdir(p)
#else
#define mkdir_one(p) mkdir(p, 0777)
#endif

/* 64-bit file offsets: long is 32 bits on Windows, so fseek/ftell stop at 2 GiB there. */
#ifdef _WIN32
typedef __int64 file_off;
#define file_seek _fseeki64
#define file_tell _ftelli64
#else
typedef off_t file_off;
#define file_seek fseeko
#define file_tell ftello
#endif

struct zip_entry {
    char name[256];
    uint32_t method;
    uint32_t comp_size;
    uint32_t size;
    uint32_t local_offset;
};

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void make_dirs_for(const char *path) {
    char tmp[1200];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            char c = *p;
            *p = 0;
            mkdir_one(tmp);
            *p = c;
        }
    }
}

static int write_file(const char *path, const void *data, size_t size) {
    make_dirs_for(path);
    FILE *f = fopen(path, "wb");
    if (!f) {
        return 0;
    }
    int ok = fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

static struct zip_entry *read_central_directory(FILE *f, int *count) {
    if (file_seek(f, 0, SEEK_END) != 0) {
        return NULL;
    }
    file_off size = file_tell(f);
    if (size < 0) {
        return NULL;
    }
    long tail = size < 70000 ? (long)size : 70000;
    uint8_t *buf = malloc(tail ? (size_t)tail : 1);
    if (!buf || file_seek(f, size - tail, SEEK_SET) != 0 ||
        fread(buf, 1, (size_t)tail, f) != (size_t)tail) {
        free(buf);
        return NULL;
    }
    long eocd = -1;
    for (long i = tail - 22; i >= 0; --i) {
        if (rd32(buf + i) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        free(buf);
        return NULL;
    }
    int n = rd16(buf + eocd + 10);
    uint32_t cd_size = rd32(buf + eocd + 12), cd_offset = rd32(buf + eocd + 16);
    free(buf);
    /* the central directory sits before the end record, inside the file */
    file_off eocd_at = size - tail + eocd;
    if (n == 0 || cd_size == 0 || (file_off)cd_offset + (file_off)cd_size > eocd_at) {
        return NULL;
    }
    uint8_t *cd = malloc(cd_size);
    if (!cd || file_seek(f, (file_off)cd_offset, SEEK_SET) != 0 || fread(cd, 1, cd_size, f) != cd_size) {
        free(cd);
        return NULL;
    }
    struct zip_entry *entries = calloc((size_t)n, sizeof(*entries));
    if (!entries) {
        free(cd);
        return NULL;
    }
    uint32_t pos = 0;
    int got = 0;
    for (int i = 0; i < n && pos + 46 <= cd_size; ++i) {
        const uint8_t *h = cd + pos;
        if (rd32(h) != 0x02014b50u) {
            break;
        }
        uint16_t name_len = rd16(h + 28), extra = rd16(h + 30), comment = rd16(h + 32);
        if (name_len > cd_size - pos - 46) { /* the name runs past the directory */
            free(cd);
            free(entries);
            return NULL;
        }
        struct zip_entry *e = &entries[got];
        e->method = rd16(h + 10);
        e->comp_size = rd32(h + 20);
        e->size = rd32(h + 24);
        e->local_offset = rd32(h + 42);
        size_t len = name_len < sizeof(e->name) - 1 ? name_len : sizeof(e->name) - 1;
        memcpy(e->name, h + 46, len);
        e->name[len] = 0;
        got++;
        pos += 46u + name_len + extra + comment;
    }
    free(cd);
    *count = got;
    return entries;
}

/* The entry's data, e->size bytes long (callers rely on that length). */
static uint8_t *read_entry(FILE *f, const struct zip_entry *e) {
    if (e->method != 0 && e->method != 8) {
        return NULL;
    }
    /* stored: the bytes on disk are the file, so a bigger size would read past them */
    if (e->method == 0 && e->comp_size != e->size) {
        return NULL;
    }
    uint8_t local[30];
    if (file_seek(f, (file_off)e->local_offset, SEEK_SET) != 0 || fread(local, 1, 30, f) != 30 ||
        rd32(local) != 0x04034b50u) {
        return NULL;
    }
    /* deflate cannot shrink data more than ~1032:1, so a bigger size is a lie */
    if (e->method == 8 && (uint64_t)e->size > (uint64_t)e->comp_size * 1032u + 1024u) {
        return NULL;
    }
    file_off data = (file_off)e->local_offset + 30 + rd16(local + 26) + rd16(local + 28);
    if (file_seek(f, 0, SEEK_END) != 0) {
        return NULL;
    }
    file_off end = file_tell(f);
    if (end < data || (file_off)e->comp_size > end - data || file_seek(f, data, SEEK_SET) != 0) {
        return NULL;
    }
    uint8_t *comp = malloc(e->comp_size ? e->comp_size : 1);
    if (!comp || fread(comp, 1, e->comp_size, f) != e->comp_size) {
        free(comp);
        return NULL;
    }
    if (e->method == 0) {
        return comp;
    }
    uint8_t *out = malloc(e->size ? e->size : 1);
    if (!out) {
        free(comp);
        return NULL;
    }
    int n = stbi_zlib_decode_noheader_buffer((char *)out, (int)e->size, (const char *)comp,
                                             (int)e->comp_size);
    free(comp);
    if (n != (int)e->size) {
        free(out);
        return NULL;
    }
    return out;
}

static void *lzma_alloc(ISzAllocPtr p, size_t size) {
    (void)p;
    return malloc(size);
}

static void lzma_free(ISzAllocPtr p, void *address) {
    (void)p;
    free(address);
}

/* LZMA "alone": 5 property bytes, 64-bit size, stream. */
static uint8_t *lzma_alone_decode(const uint8_t *src, size_t src_len, size_t *out_len) {
    if (src_len < 13) {
        return NULL;
    }
    uint64_t size = 0;
    for (int i = 0; i < 8; ++i) {
        size |= (uint64_t)src[5 + i] << (8 * i);
    }
    if (size == 0 || size > (256u << 20)) {
        return NULL;
    }
    uint8_t *out = malloc((size_t)size);
    if (!out) {
        return NULL;
    }
    SizeT dest_len = (SizeT)size, in_len = src_len - 13;
    ELzmaStatus status;
    ISzAlloc alloc = {lzma_alloc, lzma_free};
    SRes res = LzmaDecode(out, &dest_len, src + 13, &in_len, src, 5, LZMA_FINISH_ANY, &status, &alloc);
    if (res != SZ_OK || dest_len != size) {
        free(out);
        return NULL;
    }
    *out_len = (size_t)size;
    return out;
}

static int has_entry(const struct zip_entry *entries, int count, const char *name) {
    for (int i = 0; i < count; ++i) {
        if (strcmp(entries[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

int install_apk(const char *apk_path, const char *root, enum game_mode *mode_out, char *error,
                size_t error_size) {
    FILE *f = fopen(apk_path, "rb");
    if (!f) {
        snprintf(error, error_size, "Nao foi possivel abrir o APK.");
        return 0;
    }
    int count = 0;
    struct zip_entry *entries = read_central_directory(f, &count);
    if (!entries || !has_entry(entries, count, "assets/boz.s3e")) {
        fclose(f);
        free(entries);
        snprintf(error, error_size, "Este arquivo nao e o APK do Black Ops Zombies.");
        return 0;
    }
    enum game_mode mode =
        has_entry(entries, count, "assets/boz_menus.json") ? MODE_REIMAGINED : MODE_ORIGINAL;
    char dir[1100];
    snprintf(dir, sizeof(dir), "%s/%s/assets", root, mode == MODE_REIMAGINED ? "reimagined" : "original");
    int written = 0;
    for (int i = 0; i < count; ++i) {
        const struct zip_entry *e = &entries[i];
        size_t len = strlen(e->name);
        if (strncmp(e->name, "assets/", 7) != 0 || e->name[len - 1] == '/' ||
            strstr(e->name, "..") || strcmp(e->name, "assets/su.save") == 0) {
            continue;
        }
        uint8_t *data = read_entry(f, e);
        if (!data) {
            snprintf(error, error_size, "Falha ao ler %s do APK.", e->name);
            fclose(f);
            free(entries);
            return 0;
        }
        char path[1400];
        snprintf(path, sizeof(path), "%s/%s", dir, e->name + 7);
        if (!write_file(path, data, e->size)) {
            free(data);
            snprintf(error, error_size, "Falha ao gravar %s.", path);
            fclose(f);
            free(entries);
            return 0;
        }
        if (strcmp(e->name, "assets/boz.s3e") == 0) {
            size_t raw_len = 0;
            uint8_t *raw = lzma_alone_decode(data, e->size, &raw_len);
            if (!raw) {
                free(data);
                snprintf(error, error_size, "Falha ao descompactar o boz.s3e.");
                fclose(f);
                free(entries);
                return 0;
            }
            snprintf(path, sizeof(path), "%s/boz.s3e.unpacked", dir);
            write_file(path, raw, raw_len);
            free(raw);
        }
        free(data);
        written++;
    }
    fclose(f);
    free(entries);
    log_info("installed %d files from %s into %s", written, apk_path, dir);
    *mode_out = mode;
    return 1;
}

const char *mode_dir_name(enum game_mode mode) {
    return mode == MODE_REIMAGINED ? "reimagined" : mode == MODE_IOS ? "ios" : "original";
}

int install_present(const char *root, enum game_mode mode) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s/assets/boz.s3e.unpacked", root, mode_dir_name(mode));
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    if (mode == MODE_IOS) { /* the iOS data archive lives next to the image */
        snprintf(path, sizeof(path), "%s/ios/assets/blackops_gles1.dz", root);
        return stat(path, &st) == 0;
    }
    return 1;
}

/* ---- iOS IPA ----
 * Marmalade's iOS deployment links the game image into the app executable instead of shipping
 * boz.s3e: in the armv7 slice, section __TEXT,__symbolstub1 holds the image's code (already
 * laid out for base 0x4a000000), __S3E_DATA,s3e_data its data part, and __S3E_META,s3e_meta an
 * S3E header with the config and fixup tables. The data part is scrambled with the eexec stream
 * cipher (r = (c + r) * 52845 + 22719) under a 16-bit key the loader sets at run time; the key is
 * recovered here from the image's own relocation table (every relocated word must point into the
 * image). The result is a plain S3E file, identical in layout to the Android one. */

struct macho_section {
    uint32_t addr, size, offset;
};

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Finds the armv7 Mach-O inside EXE (fat or thin) and returns its offset, or -1. */
static long armv7_slice(const uint8_t *exe, size_t size) {
    if (size >= 8 && be32(exe) == 0xcafebabeu) {
        uint32_t n = be32(exe + 4);
        for (uint32_t i = 0; i < n && 8 + (i + 1) * 20 <= size; ++i) {
            const uint8_t *a = exe + 8 + i * 20;
            if (be32(a) == 12 /* ARM */ && be32(a + 4) == 9 /* V7 */ && be32(a + 8) < size) {
                return (long)be32(a + 8);
            }
        }
        return -1;
    }
    return size >= 28 && rd32(exe) == 0xfeedfaceu && rd32(exe + 4) == 12 ? 0 : -1;
}

static bool macho_find(const uint8_t *m, size_t size, const char *seg, const char *sect,
                       struct macho_section *out) {
    if (size < 28 || rd32(m) != 0xfeedfaceu) {
        return false;
    }
    uint32_t ncmds = rd32(m + 16), pos = 28;
    for (uint32_t i = 0; i < ncmds && pos + 8 <= size; ++i) {
        uint32_t cmd = rd32(m + pos), cmdsize = rd32(m + pos + 4);
        if (cmdsize < 8 || pos + cmdsize > size) {
            return false;
        }
        if (cmd == 1 /* LC_SEGMENT */ && cmdsize >= 56) {
            uint32_t nsects = rd32(m + pos + 48);
            for (uint32_t k = 0; k < nsects && 56 + (k + 1) * 68 <= cmdsize; ++k) {
                const uint8_t *s = m + pos + 56 + k * 68;
                if (strncmp((const char *)s, sect, 16) == 0 && strncmp((const char *)s + 16, seg, 16) == 0) {
                    out->addr = rd32(s + 32);
                    out->size = rd32(s + 36);
                    out->offset = rd32(s + 40);
                    return out->offset + (uint64_t)out->size <= size;
                }
            }
        }
        pos += cmdsize;
    }
    return false;
}

static void eexec_decode(uint8_t *data, size_t len, uint32_t key, size_t limit) {
    uint32_t r = key;
    for (size_t i = 0; i < len && i < limit; ++i) {
        uint8_t c = data[i];
        data[i] = c ^ (uint8_t)(r >> 8);
        r = ((uint32_t)c + (r & 0xffffu)) * 52845u + 22719u;
    }
}

/* Builds the S3E file from the executable. Returns a malloc'd buffer or NULL. */
static uint8_t *ios_image_from_exe(const uint8_t *exe, size_t exe_size, size_t *out_size,
                                   char *error, size_t error_size) {
    long slice = armv7_slice(exe, exe_size);
    if (slice < 0) {
        snprintf(error, error_size, "O IPA nao tem o executavel armv7.");
        return NULL;
    }
    const uint8_t *m = exe + slice;
    size_t msize = exe_size - (size_t)slice;
    struct macho_section text, data, meta;
    if (!macho_find(m, msize, "__TEXT", "__symbolstub1", &text) ||
        !macho_find(m, msize, "__S3E_DATA", "s3e_data", &data) ||
        !macho_find(m, msize, "__S3E_META", "s3e_meta", &meta)) {
        snprintf(error, error_size, "Este IPA nao tem a imagem do jogo Marmalade.");
        return NULL;
    }
    /* the S3E header sits near the start of the meta section */
    const uint8_t *h = NULL;
    for (uint32_t o = 0; o + 76 <= meta.size && o < 0x1000; o += 4) {
        if (rd32(m + meta.offset + o) == 0x55334558u) {
            h = m + meta.offset + o;
            break;
        }
    }
    if (!h || text.addr + text.size != data.addr) {
        snprintf(error, error_size, "Formato da imagem do jogo no IPA nao reconhecido.");
        return NULL;
    }
    uint32_t meta_left = meta.size - (uint32_t)(h - (m + meta.offset));
    uint32_t fix_off = rd32(h + 12), fix_size = rd32(h + 16), code_off = rd32(h + 20);
    uint32_t code_file = rd32(h + 24), code_mem = rd32(h + 28), base = rd32(h + 52);
    if (code_off > meta_left || (uint64_t)fix_off + fix_size > code_off || code_file > text.size + data.size ||
        code_file <= text.size) {
        snprintf(error, error_size, "Cabecalho da imagem do jogo no IPA invalido.");
        return NULL;
    }
    /* relocation table (fixup record type 1): u32 count, then u32 code offsets */
    const uint32_t *relocs = NULL;
    uint32_t reloc_count = 0;
    for (uint32_t pos = fix_off; pos + 12 <= fix_off + fix_size;) {
        uint32_t type = rd32(h + pos), size = rd32(h + pos + 4);
        if (size < 8 || pos + size > fix_off + fix_size) {
            break;
        }
        if (type == 1 && size >= 12 && rd32(h + pos + 8) <= (size - 12) / 4) {
            reloc_count = rd32(h + pos + 8);
            relocs = (const uint32_t *)(h + pos + 12);
        }
        pos += size;
    }
    if (!reloc_count) {
        snprintf(error, error_size, "Tabela de relocacao da imagem do jogo nao encontrada.");
        return NULL;
    }

    size_t total = code_off + (size_t)code_file;
    uint8_t *out = malloc(total);
    if (!out) {
        snprintf(error, error_size, "Memoria insuficiente.");
        return NULL;
    }
    memcpy(out, h, code_off);
    uint8_t *code = out + code_off;
    memcpy(code, m + text.offset, text.size);
    uint8_t *dpart = code + text.size;
    uint32_t dlen = code_file - text.size;
    memcpy(dpart, m + data.offset, dlen);

    /* key search on the first relocations that land in the data part */
    uint32_t probe[32], probes = 0, limit = 0;
    for (uint32_t i = 0; i < reloc_count && probes < 32; ++i) {
        uint32_t off = rd32((const uint8_t *)&relocs[i]);
        if (off >= text.size && off + 4 <= code_file) {
            probe[probes++] = off - text.size;
            if (off - text.size + 4 > limit) limit = off - text.size + 4;
        }
    }
    uint8_t *scratch = malloc(limit ? limit : 1);
    if (!scratch) {
        free(out);
        snprintf(error, error_size, "Memoria insuficiente.");
        return NULL;
    }
    long key = -1;
    for (uint32_t k = 0; probes && k < 65536 && key < 0; ++k) {
        memcpy(scratch, m + data.offset, limit);
        eexec_decode(scratch, limit, k, limit);
        uint32_t hit = 0;
        for (uint32_t i = 0; i < probes; ++i) {
            uint32_t w = rd32(scratch + probe[i]);
            hit += w >= base && w < base + code_mem;
        }
        if (hit == probes) key = (long)k;
    }
    free(scratch);
    if (key < 0) {
        free(out);
        snprintf(error, error_size, "Nao foi possivel decodificar os dados do jogo no IPA.");
        return NULL;
    }
    eexec_decode(dpart, dlen, (uint32_t)key, dlen);

    uint32_t bad = 0;
    for (uint32_t i = 0; i < reloc_count; ++i) {
        uint32_t off = rd32((const uint8_t *)&relocs[i]);
        uint32_t w = off + 4 <= code_file ? rd32(code + off) : 0;
        bad += !(w >= base && w < base + code_mem);
    }
    if (bad) {
        free(out);
        snprintf(error, error_size, "Imagem do jogo no IPA inconsistente (%u relocacoes).", bad);
        return NULL;
    }
    log_info("IPA: game image %u+%u bytes, data key 0x%04lx, %u relocations checked", text.size, dlen,
             key, reloc_count);
    *out_size = total;
    return out;
}

static bool zip_is_ipa(const struct zip_entry *entries, int count) {
    for (int i = 0; i < count; ++i) {
        if (strncmp(entries[i].name, "Payload/", 8) == 0 && strstr(entries[i].name, ".app/")) {
            return true;
        }
    }
    return false;
}

int install_ipa(const char *ipa_path, const char *root, char *error, size_t error_size) {
    FILE *f = fopen(ipa_path, "rb");
    if (!f) {
        snprintf(error, error_size, "Nao foi possivel abrir o IPA.");
        return 0;
    }
    int count = 0;
    struct zip_entry *entries = read_central_directory(f, &count);
    if (!entries || !zip_is_ipa(entries, count)) {
        fclose(f);
        free(entries);
        snprintf(error, error_size, "Este arquivo nao e um IPA.");
        return 0;
    }
    char dir[1100];
    snprintf(dir, sizeof(dir), "%s/ios/assets", root);
    const struct zip_entry *gles1 = NULL;
    uint8_t *image = NULL;
    size_t image_size = 0;
    int written = 0, ok = 1;
    for (int i = 0; i < count && ok; ++i) {
        const struct zip_entry *e = &entries[i];
        const char *app = strstr(e->name, ".app/");
        if (strncmp(e->name, "Payload/", 8) != 0 || !app || strstr(e->name, "..")) {
            continue;
        }
        const char *rel = app + 5;
        size_t len = strlen(rel);
        if (!len || rel[len - 1] == '/') {
            continue;
        }
        bool music = strncmp(rel, "blackops-music/", 15) == 0 || strncmp(rel, "deadops-music/", 14) == 0;
        if (strcmp(rel, "blackops_gles1.dz") == 0) {
            gles1 = e;
            continue; /* the big one goes last */
        }
        bool exe_candidate = !strchr(rel, '/') && !strchr(rel, '.') && e->size > (1u << 20);
        if (!music && !exe_candidate) {
            continue;
        }
        uint8_t *data = read_entry(f, e);
        if (!data) {
            snprintf(error, error_size, "Falha ao ler %s do IPA.", rel);
            ok = 0;
            break;
        }
        if (exe_candidate && !image) {
            image = ios_image_from_exe(data, e->size, &image_size, error, error_size);
            free(data);
            if (!image) {
                ok = 0;
            }
            continue;
        }
        char path[1400];
        snprintf(path, sizeof(path), "%s/%s", dir, rel);
        if (!write_file(path, data, e->size)) {
            snprintf(error, error_size, "Falha ao gravar %s.", path);
            ok = 0;
        }
        free(data);
        written++;
    }
    if (ok && !image) {
        snprintf(error, error_size, "O executavel do jogo nao foi encontrado no IPA.");
        ok = 0;
    }
    if (ok && !gles1) {
        snprintf(error, error_size, "O IPA nao tem blackops_gles1.dz.");
        ok = 0;
    }
    if (ok) {
        char path[1400];
        snprintf(path, sizeof(path), "%s/boz.s3e.unpacked", dir);
        ok = write_file(path, image, image_size);
        if (!ok) snprintf(error, error_size, "Falha ao gravar %s.", path);
    }
    if (ok) {
        uint8_t *data = read_entry(f, gles1);
        char path[1400];
        snprintf(path, sizeof(path), "%s/blackops_gles1.dz", dir);
        ok = data && write_file(path, data, gles1->size);
        if (!ok) snprintf(error, error_size, "Falha ao extrair blackops_gles1.dz.");
        free(data);
        written += ok;
    }
    free(image);
    fclose(f);
    free(entries);
    if (ok) {
        log_info("installed iOS game (%d files) from %s into %s", written + 1, ipa_path, dir);
    }
    return ok;
}

int install_package(const char *path, const char *root, enum game_mode *mode_out, char *error,
                    size_t error_size) {
    FILE *f = fopen(path, "rb");
    int count = 0;
    struct zip_entry *entries = f ? read_central_directory(f, &count) : NULL;
    bool ipa = entries && zip_is_ipa(entries, count);
    free(entries);
    if (f) fclose(f);
    if (ipa) {
        if (!install_ipa(path, root, error, error_size)) return 0;
        *mode_out = MODE_IOS;
        return 1;
    }
    return install_apk(path, root, mode_out, error, error_size);
}
