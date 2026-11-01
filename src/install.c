/*
 * Installs a game folder from an APK the player owns:
 *   <root>/original/assets     from the Android APK 1.0.11 (com.activision.boz)
 *   <root>/reimagined/assets   from boz-reimagined's BOZ-TV.apk (same boz.s3e as the TV)
 * The OBB is never copied; its path is remembered in <root>/launcher.cfg.
 */
#include "install.h"
#include "log.h"

#include "../third_party/LzmaDec.h"
#include "../third_party/stb_image.h"

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
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    long tail = size < 70000 ? size : 70000;
    uint8_t *buf = malloc((size_t)tail);
    fseek(f, size - tail, SEEK_SET);
    if (fread(buf, 1, (size_t)tail, f) != (size_t)tail) {
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
    uint8_t *cd = malloc(cd_size);
    fseek(f, (long)cd_offset, SEEK_SET);
    if (fread(cd, 1, cd_size, f) != cd_size) {
        free(cd);
        return NULL;
    }
    struct zip_entry *entries = calloc((size_t)n, sizeof(*entries));
    uint32_t pos = 0;
    int got = 0;
    for (int i = 0; i < n && pos + 46 <= cd_size; ++i) {
        const uint8_t *h = cd + pos;
        if (rd32(h) != 0x02014b50u) {
            break;
        }
        uint16_t name_len = rd16(h + 28), extra = rd16(h + 30), comment = rd16(h + 32);
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

static uint8_t *read_entry(FILE *f, const struct zip_entry *e) {
    uint8_t local[30];
    fseek(f, (long)e->local_offset, SEEK_SET);
    if (fread(local, 1, 30, f) != 30 || rd32(local) != 0x04034b50u) {
        return NULL;
    }
    fseek(f, (long)e->local_offset + 30 + rd16(local + 26) + rd16(local + 28), SEEK_SET);
    uint8_t *comp = malloc(e->comp_size ? e->comp_size : 1);
    if (fread(comp, 1, e->comp_size, f) != e->comp_size) {
        free(comp);
        return NULL;
    }
    if (e->method == 0) {
        return comp;
    }
    if (e->method != 8) {
        free(comp);
        return NULL;
    }
    uint8_t *out = malloc(e->size ? e->size : 1);
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

int install_present(const char *root, enum game_mode mode) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s/assets/boz.s3e.unpacked", root,
             mode == MODE_REIMAGINED ? "reimagined" : "original");
    struct stat st;
    return stat(path, &st) == 0;
}
