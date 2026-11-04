/* Installer checks (src/install.c): legitimate APK/IPA layouts install byte for byte, and
 * malformed archives are rejected without touching memory they do not own.
 *   cc -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc tools/test_install.c \
 *      src/install.c third_party/LzmaDec.c -lm -o /tmp/test_install && /tmp/test_install
 * Every archive is synthetic (no game data): small ZIPs written here, and for the IPA a thin
 * armv7 Mach-O with the three Marmalade sections and an eexec-scrambled data part. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../third_party/stb_image.h"

#include "install.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif

/* Under ASan, allocations above 512 MiB return NULL as on a phone without overcommit: an archive
 * that makes the installer ask for gigabytes must be rejected, not crash on the NULL. */
const char *__asan_default_options(void);
const char *__asan_default_options(void) {
    return "allocator_may_return_null=1:max_allocation_size_mb=512";
}

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("FAIL %s:%d ", __FILE__, __LINE__);                                             \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

/* ---- byte buffers ---- */

typedef struct {
    uint8_t *p;
    size_t n, cap;
} buf;

static void put(buf *b, const void *data, size_t n) {
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 64;
        b->p = realloc(b->p, b->cap);
    }
    if (n) {
        if (data) {
            memcpy(b->p + b->n, data, n);
        } else {
            memset(b->p + b->n, 0, n);
        }
    }
    b->n += n;
}
static void u16(buf *b, uint32_t v) {
    uint8_t x[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    put(b, x, 2);
}
static void u32(buf *b, uint32_t v) {
    uint8_t x[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(b, x, 4);
}
static void set32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* ---- ZIP writer: every field can be set, so the malformed cases are one call each ---- */

typedef struct {
    buf data, cd;
    uint32_t count;
    uint32_t base; /* bytes of hole before the archive (sparse file) */
} zip;

struct zip_fields {
    uint32_t comp;     /* compressed size in the central directory */
    uint32_t size;     /* uncompressed size in the central directory */
    uint16_t extra;    /* extra field bytes (zeros) in both headers */
    uint16_t comment;  /* file comment bytes in the central directory */
};

static void zip_add_fields(zip *z, const char *name, uint16_t method, const void *payload,
                           size_t payload_len, struct zip_fields f) {
    uint32_t offset = z->base + (uint32_t)z->data.n;
    uint16_t name_len = (uint16_t)strlen(name);
    u32(&z->data, 0x04034b50u);
    u16(&z->data, 20);
    u16(&z->data, 0);
    u16(&z->data, method);
    u16(&z->data, 0);
    u16(&z->data, 0);
    u32(&z->data, 0);
    u32(&z->data, f.comp);
    u32(&z->data, f.size);
    u16(&z->data, name_len);
    u16(&z->data, f.extra);
    put(&z->data, name, name_len);
    put(&z->data, NULL, f.extra);
    put(&z->data, payload, payload_len);

    u32(&z->cd, 0x02014b50u);
    u16(&z->cd, 20);
    u16(&z->cd, 20);
    u16(&z->cd, 0);
    u16(&z->cd, method);
    u16(&z->cd, 0);
    u16(&z->cd, 0);
    u32(&z->cd, 0);
    u32(&z->cd, f.comp);
    u32(&z->cd, f.size);
    u16(&z->cd, name_len);
    u16(&z->cd, f.extra);
    u16(&z->cd, f.comment);
    u16(&z->cd, 0);
    u16(&z->cd, 0);
    u32(&z->cd, 0);
    u32(&z->cd, offset);
    put(&z->cd, name, name_len);
    put(&z->cd, NULL, f.extra);
    put(&z->cd, NULL, f.comment);
    z->count++;
}

static void zip_add(zip *z, const char *name, uint16_t method, const void *payload,
                    size_t payload_len, size_t size) {
    struct zip_fields f = {(uint32_t)payload_len, (uint32_t)size, 0, 0};
    zip_add_fields(z, name, method, payload, payload_len, f);
}

/* Writes data + central directory + EOCD. The EOCD values can be overridden (0 = real). */
struct eocd_fields {
    uint32_t count, cd_size, cd_offset;
};

static void zip_write_eocd(zip *z, const char *path, struct eocd_fields e) {
    buf out = {0};
    put(&out, z->data.p, z->data.n);
    put(&out, z->cd.p, z->cd.n);
    u32(&out, 0x06054b50u);
    u16(&out, 0);
    u16(&out, 0);
    u16(&out, e.count ? e.count : z->count);
    u16(&out, e.count ? e.count : z->count);
    u32(&out, e.cd_size ? e.cd_size : (uint32_t)z->cd.n);
    u32(&out, e.cd_offset ? e.cd_offset : z->base + (uint32_t)z->data.n);
    u16(&out, 0);
    FILE *f = fopen(path, "wb");
    if (f) {
#ifdef _WIN32
        _fseeki64(f, z->base, SEEK_SET);
#else
        fseeko(f, (off_t)z->base, SEEK_SET);
#endif
        fwrite(out.p, 1, out.n, f);
        fclose(f);
    }
    free(out.p);
    free(z->data.p);
    free(z->cd.p);
    memset(z, 0, sizeof(*z));
}

static void zip_write(zip *z, const char *path) {
    struct eocd_fields e = {0};
    zip_write_eocd(z, path, e);
}

/* ---- payloads ---- */

/* LZMA "alone" stream of k_image (python: lzma.compress(FORMAT_ALONE, preset 0, dict 4096), with
 * the real size patched into bytes 5..12). */
static const char k_image[] = "S3E test image for tools/test_install.c\n"
                              "S3E test image for tools/test_install.c\n"
                              "S3E test image for tools/test_install.c\n"
                              "S3E test image for tools/test_install.c\n"
                              "S3E test image for tools/test_install.c\n"
                              "S3E test image for tools/test_install.c\n";
static const uint8_t k_image_lzma[] = {
    0x5d, 0x00, 0x10, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x29,
    0x8c, 0xc4, 0xa2, 0x13, 0x96, 0x64, 0xf8, 0x61, 0x8d, 0x1c, 0xd9, 0x45, 0x62, 0x22, 0xa7,
    0x31, 0x5f, 0x56, 0x5e, 0x89, 0x30, 0x40, 0xee, 0x61, 0xdf, 0x20, 0x57, 0x21, 0xa3, 0x38,
    0x2d, 0x50, 0x17, 0x82, 0xcd, 0xef, 0xe0, 0xc0, 0xf3, 0x17, 0xca, 0x2b, 0xd9, 0x3f, 0x9f,
    0xff, 0xd5, 0xaf, 0x30, 0x00};

/* Raw deflate of k_text (python: zlib.compressobj(9, DEFLATED, -15)). */
static const char k_text[] = "{\"menus\":\"boz_menus.json test, deflated\"}\n"
                             "{\"menus\":\"boz_menus.json test, deflated\"}\n"
                             "{\"menus\":\"boz_menus.json test, deflated\"}\n"
                             "{\"menus\":\"boz_menus.json test, deflated\"}\n";
static const uint8_t k_text_deflate[] = {
    0xab, 0x56, 0xca, 0x4d, 0xcd, 0x2b, 0x2d, 0x56, 0xb2, 0x52, 0x4a, 0xca, 0xaf, 0x8a, 0x07,
    0xb3, 0xf5, 0xb2, 0x8a, 0xf3, 0xf3, 0x14, 0x4a, 0x52, 0x8b, 0x4b, 0x74, 0x14, 0x52, 0x52,
    0xd3, 0x72, 0x12, 0x4b, 0x52, 0x53, 0x94, 0x6a, 0xb9, 0xaa, 0x07, 0x54, 0x25, 0x00};

/* ---- the iOS executable ----
 * Thin armv7 Mach-O, one LC_SEGMENT with __TEXT,__symbolstub1 (image code), __S3E_DATA,s3e_data
 * (image data, eexec-scrambled) and __S3E_META,s3e_meta (S3E header + fixups). Padded past 1 MiB:
 * the installer only treats big extension-less files as the executable. */

enum {
    IOS_TEXT_OFF = 0x200,
    IOS_TEXT_SIZE = 0x100,
    IOS_DATA_OFF = IOS_TEXT_OFF + IOS_TEXT_SIZE,
    IOS_DATA_SIZE = 0x100,
    IOS_META_OFF = IOS_DATA_OFF + IOS_DATA_SIZE,
    IOS_META_SIZE = 0x200,
    IOS_FIX_OFF = 76,
    IOS_RELOCS = 40, /* 4 in the text part, 36 in the data part: more than the 32 key probes */
    IOS_CODE_OFF = 0x100,
    IOS_BASE = 0x4a000000u,
    IOS_CODE_MEM = 0x10000u,
    IOS_KEY = 0x1234,
    IOS_EXE_SIZE = (1 << 20) + 4096,
};

/* Fields a malformed case overrides (0 = the valid value). */
struct ios_fields {
    uint32_t cmdsize;
    uint32_t text_size;
    uint32_t data_size;
    uint32_t fix_record_size;
    uint32_t reloc0;
    int reloc0_set;
    uint32_t reloc_last; /* replaces the last relocation, after the 32 key probes */
    uint32_t nsects;
    int rename_sections; /* none of the three sections has the name the installer looks for */
};

static void put_section(uint8_t *s, const char *sect, const char *seg, uint32_t addr, uint32_t size,
                        uint32_t offset) {
    memset(s, 0, 68);
    strncpy((char *)s, sect, 16);
    strncpy((char *)s + 16, seg, 16);
    set32(s + 32, addr);
    set32(s + 36, size);
    set32(s + 40, offset);
}

/* The executable, and the boz.s3e.unpacked the installer must make from it. */
static uint8_t *ios_exe(struct ios_fields f, uint8_t **expected, size_t *expected_size) {
    uint8_t *exe = calloc(1, IOS_EXE_SIZE);
    set32(exe, 0xfeedfaceu);
    set32(exe + 4, 12); /* ARM */
    set32(exe + 8, 9);  /* V7 */
    set32(exe + 12, 2);
    set32(exe + 16, 1); /* ncmds */
    set32(exe + 20, 56 + 3 * 68);
    uint8_t *cmd = exe + 28;
    set32(cmd, 1); /* LC_SEGMENT */
    set32(cmd + 4, f.cmdsize ? f.cmdsize : 56 + 3 * 68);
    set32(cmd + 48, f.nsects ? f.nsects : 3);
    uint32_t text_addr = IOS_BASE + IOS_CODE_OFF;
    put_section(cmd + 56, "__symbolstub1", "__TEXT", text_addr,
                f.text_size ? f.text_size : IOS_TEXT_SIZE, IOS_TEXT_OFF);
    put_section(cmd + 56 + 68, "s3e_data", "__S3E_DATA", text_addr + IOS_TEXT_SIZE,
                f.data_size ? f.data_size : IOS_DATA_SIZE, IOS_DATA_OFF);
    put_section(cmd + 56 + 136, "s3e_meta", "__S3E_META", 0, IOS_META_SIZE, IOS_META_OFF);
    if (f.rename_sections) {
        cmd[56] = cmd[56 + 68] = cmd[56 + 136] = 'X';
    }

    /* image text: plain; data: plain here, scrambled below */
    uint8_t *text = exe + IOS_TEXT_OFF, plain[IOS_DATA_SIZE];
    for (int i = 0; i < IOS_TEXT_SIZE; ++i) {
        text[i] = (uint8_t)(i * 7 + 3);
    }
    for (int i = 0; i < IOS_DATA_SIZE; ++i) {
        plain[i] = (uint8_t)(i * 13 + 5);
    }
    /* relocations: 4 in the text part, 36 in the data part; each word points into the image */
    uint32_t relocs[IOS_RELOCS];
    for (int i = 0; i < IOS_RELOCS; ++i) {
        relocs[i] = i < 4 ? (uint32_t)i * 16 : IOS_TEXT_SIZE + (uint32_t)(i - 4) * 4;
        uint8_t *word = i < 4 ? text + relocs[i] : plain + (relocs[i] - IOS_TEXT_SIZE);
        set32(word, IOS_BASE + 0x100 + (uint32_t)i * 8);
    }
    /* S3E header + fixup table (type 1: count, then code offsets) */
    uint8_t *h = exe + IOS_META_OFF;
    memset(h, 0xee, IOS_META_SIZE);
    memset(h, 0, IOS_CODE_OFF);
    set32(h, 0x55334558u);
    uint32_t fix_size = 12 + 4 * IOS_RELOCS;
    set32(h + 12, IOS_FIX_OFF);
    set32(h + 16, fix_size);
    set32(h + 20, IOS_CODE_OFF);
    set32(h + 24, IOS_TEXT_SIZE + IOS_DATA_SIZE);
    set32(h + 28, IOS_CODE_MEM);
    set32(h + 52, IOS_BASE);
    uint8_t *fix = h + IOS_FIX_OFF;
    set32(fix, 1);
    set32(fix + 4, f.fix_record_size ? f.fix_record_size : fix_size);
    set32(fix + 8, IOS_RELOCS);
    for (int i = 0; i < IOS_RELOCS; ++i) {
        set32(fix + 12 + 4 * i, i == 0 && f.reloc0_set ? f.reloc0 : relocs[i]);
    }
    if (f.reloc_last) {
        set32(fix + 12 + 4 * (IOS_RELOCS - 1), f.reloc_last);
    }
    if (f.fix_record_size) {
        /* a record just before the table that leads back into it: a wrapped size loops */
        set32(h + 68, 0);
        set32(h + 72, IOS_FIX_OFF - 68);
    }
    /* eexec: c = p ^ (r >> 8), r = (c + r) * 52845 + 22719 */
    uint32_t r = IOS_KEY;
    for (int i = 0; i < IOS_DATA_SIZE; ++i) {
        uint8_t c = plain[i] ^ (uint8_t)(r >> 8);
        exe[IOS_DATA_OFF + i] = c;
        r = ((uint32_t)c + (r & 0xffffu)) * 52845u + 22719u;
    }
    if (expected) {
        *expected_size = IOS_CODE_OFF + IOS_TEXT_SIZE + IOS_DATA_SIZE;
        *expected = malloc(*expected_size);
        memcpy(*expected, h, IOS_CODE_OFF);
        memcpy(*expected + IOS_CODE_OFF, text, IOS_TEXT_SIZE);
        memcpy(*expected + IOS_CODE_OFF + IOS_TEXT_SIZE, plain, IOS_DATA_SIZE);
    }
    return exe;
}

/* ---- files ---- */

static char g_tmp[512];

static void path_of(char *out, size_t size, const char *rel) {
    snprintf(out, size, "%s/%s", g_tmp, rel);
}

static uint8_t *slurp(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    buf b = {0};
    uint8_t chunk[4096];
    size_t got;
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        put(&b, chunk, got);
    }
    fclose(f);
    put(&b, "", 1);
    *size = b.n - 1;
    return b.p;
}

static int file_equals(const char *root, const char *rel, const void *data, size_t size) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, rel);
    size_t got = 0;
    uint8_t *content = slurp(path, &got);
    int same = content && got == size && memcmp(content, data, size) == 0;
    free(content);
    return same;
}

static int file_exists(const char *root, const char *rel) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, rel);
    struct stat st;
    return stat(path, &st) == 0;
}

/* A fresh data root per case. */
static void new_root(char *root, size_t size, const char *name) {
    path_of(root, size, name);
#ifdef _WIN32
    _mkdir(root);
#else
    mkdir(root, 0777);
#endif
}

/* ---- legitimate archives ---- */

static void apk_entries(zip *z) {
    zip_add(z, "AndroidManifest.xml", 0, "manifest", 8, 8);
    zip_add(z, "assets/", 0, NULL, 0, 0);
    zip_add(z, "assets/boz.s3e", 0, k_image_lzma, sizeof(k_image_lzma), sizeof(k_image_lzma));
    zip_add(z, "assets/data/", 0, NULL, 0, 0);
    zip_add(z, "assets/data/menu.txt", 8, k_text_deflate, sizeof(k_text_deflate),
            sizeof(k_text) - 1);
    struct zip_fields with_extra = {5, 5, 4, 3};
    zip_add_fields(z, "assets/data/extra.bin", 0, "extra", 5, with_extra);
    zip_add(z, "assets/su.save", 0, "save", 4, 4);
    zip_add(z, "assets/../escape.txt", 0, "escape", 6, 6);
    zip_add(z, "lib/armeabi/libs3e_android.so", 0, "ELF", 3, 3);
}

static void test_apk_original(void) {
    char apk[1024], root[1024], error[256] = "";
    path_of(apk, sizeof(apk), "original.apk");
    zip z = {0};
    apk_entries(&z);
    zip_write(&z, apk);
    new_root(root, sizeof(root), "root-original");
    enum game_mode mode = MODE_COUNT;
    CHECK(install_package(apk, root, &mode, error, sizeof(error)), "original APK: %s", error);
    CHECK(mode == MODE_ORIGINAL, "original APK mode %d", mode);
    CHECK(file_equals(root, "original/assets/boz.s3e", k_image_lzma, sizeof(k_image_lzma)),
          "boz.s3e copied as is");
    CHECK(file_equals(root, "original/assets/boz.s3e.unpacked", k_image, sizeof(k_image) - 1),
          "boz.s3e.unpacked is the LZMA payload");
    CHECK(file_equals(root, "original/assets/data/menu.txt", k_text, sizeof(k_text) - 1),
          "deflated entry");
    CHECK(file_equals(root, "original/assets/data/extra.bin", "extra", 5),
          "entry with extra field and comment");
    CHECK(!file_exists(root, "original/assets/su.save"), "su.save is skipped");
    CHECK(!file_exists(root, "original/escape.txt") && !file_exists(root, "escape.txt"),
          "'..' entries are skipped");
    CHECK(install_present(root, MODE_ORIGINAL), "install_present after the original APK");
}

static void test_apk_reimagined(void) {
    char apk[1024], root[1024], error[256] = "";
    path_of(apk, sizeof(apk), "reimagined.apk");
    zip z = {0};
    apk_entries(&z);
    zip_add(&z, "assets/boz_menus.json", 8, k_text_deflate, sizeof(k_text_deflate),
            sizeof(k_text) - 1);
    zip_write(&z, apk);
    new_root(root, sizeof(root), "root-reimagined");
    enum game_mode mode = MODE_COUNT;
    CHECK(install_package(apk, root, &mode, error, sizeof(error)), "reimagined APK: %s", error);
    CHECK(mode == MODE_REIMAGINED, "reimagined APK mode %d", mode);
    CHECK(file_equals(root, "reimagined/assets/boz_menus.json", k_text, sizeof(k_text) - 1),
          "boz_menus.json");
    CHECK(file_equals(root, "reimagined/assets/boz.s3e.unpacked", k_image, sizeof(k_image) - 1),
          "reimagined boz.s3e.unpacked");
    CHECK(install_present(root, MODE_REIMAGINED), "install_present after the reimagined APK");
}

static void ipa_write(const char *path, const uint8_t *exe) {
    zip z = {0};
    zip_add(&z, "Payload/", 0, NULL, 0, 0);
    zip_add(&z, "Payload/BOZ.app/Info.plist", 0, "plist", 5, 5);
    zip_add(&z, "Payload/BOZ.app/BOZ", 0, exe, IOS_EXE_SIZE, IOS_EXE_SIZE);
    zip_add(&z, "Payload/BOZ.app/blackops-music/theme.mp3", 0, "ID3music", 8, 8);
    zip_add(&z, "Payload/BOZ.app/deadops-music/dead.mp3", 0, "ID3dead", 7, 7);
    zip_add(&z, "Payload/BOZ.app/blackops_gles1.dz", 8, k_text_deflate, sizeof(k_text_deflate),
            sizeof(k_text) - 1);
    zip_write(&z, path);
}

static void test_ipa(void) {
    char ipa[1024], root[1024], error[256] = "";
    path_of(ipa, sizeof(ipa), "boz.ipa");
    struct ios_fields valid = {0};
    uint8_t *expected = NULL;
    size_t expected_size = 0;
    uint8_t *exe = ios_exe(valid, &expected, &expected_size);
    ipa_write(ipa, exe);
    free(exe);
    new_root(root, sizeof(root), "root-ios");
    enum game_mode mode = MODE_COUNT;
    CHECK(install_package(ipa, root, &mode, error, sizeof(error)), "IPA: %s", error);
    CHECK(mode == MODE_IOS, "IPA mode %d", mode);
    CHECK(file_equals(root, "ios/assets/boz.s3e.unpacked", expected, expected_size),
          "IPA game image rebuilt byte for byte");
    CHECK(file_equals(root, "ios/assets/blackops_gles1.dz", k_text, sizeof(k_text) - 1),
          "blackops_gles1.dz");
    CHECK(file_equals(root, "ios/assets/blackops-music/theme.mp3", "ID3music", 8), "music");
    CHECK(file_equals(root, "ios/assets/deadops-music/dead.mp3", "ID3dead", 7), "dead ops music");
    CHECK(!file_exists(root, "ios/assets/Info.plist"), "other app files are skipped");
    CHECK(install_present(root, MODE_IOS), "install_present after the IPA");
    free(expected);
}

/* ---- malformed archives: rejected, nothing installed, no out-of-bounds access (ASan) ---- */

static void expect_rejected(const char *label, const char *archive, enum game_mode mode) {
    char root[1024], error[256] = "";
    char name[256];
    snprintf(name, sizeof(name), "root-%s", label);
    new_root(root, sizeof(root), name);
    enum game_mode got = MODE_COUNT;
    CHECK(!install_package(archive, root, &got, error, sizeof(error)), "%s: accepted", label);
    CHECK(error[0], "%s: no error message", label);
    CHECK(!install_present(root, mode), "%s: left an install behind", label);
}

static void ipa_expect_rejected(const char *label, struct ios_fields f) {
    char ipa[1024];
    char name[256];
    snprintf(name, sizeof(name), "%s.ipa", label);
    path_of(ipa, sizeof(ipa), name);
    uint8_t *exe = ios_exe(f, NULL, NULL);
    ipa_write(ipa, exe);
    free(exe);
    expect_rejected(label, ipa, MODE_IOS);
}

/* I1: a stored entry whose uncompressed size is bigger than its data. */
static void test_stored_size_mismatch(void) {
    char apk[1024];
    path_of(apk, sizeof(apk), "i1-stored-image.apk");
    zip z = {0};
    struct zip_fields big = {sizeof(k_image_lzma), 4096, 0, 0};
    zip_add_fields(&z, "assets/boz.s3e", 0, k_image_lzma, sizeof(k_image_lzma), big);
    zip_write(&z, apk);
    expect_rejected("i1-stored-image", apk, MODE_ORIGINAL);

    path_of(apk, sizeof(apk), "i1-stored-asset.apk");
    struct zip_fields asset = {4, 65536, 0, 0};
    zip_add_fields(&z, "assets/data/leak.bin", 0, "leak", 4, asset);
    apk_entries(&z);
    zip_write(&z, apk);
    expect_rejected("i1-stored-asset", apk, MODE_ORIGINAL);

    char ipa[1024];
    path_of(ipa, sizeof(ipa), "i1-stored-exe.ipa");
    struct ios_fields valid = {0};
    uint8_t *exe = ios_exe(valid, NULL, NULL);
    zip_add(&z, "Payload/BOZ.app/BOZ", 0, exe, IOS_EXE_SIZE, IOS_EXE_SIZE + 65536);
    zip_add(&z, "Payload/BOZ.app/blackops_gles1.dz", 0, "dz", 2, 2);
    zip_write(&z, ipa);
    free(exe);
    expect_rejected("i1-stored-exe", ipa, MODE_IOS);
}

/* I2: a central directory entry whose name runs past the end of the directory. */
static void test_cd_name_overflow(void) {
    char apk[1024];
    path_of(apk, sizeof(apk), "i2-cd-name.apk");
    zip z = {0};
    zip_add(&z, "assets/boz.s3e", 0, k_image_lzma, sizeof(k_image_lzma), sizeof(k_image_lzma));
    zip_add(&z, "assets/x", 0, "x", 1, 1);
    /* last entry: name length 250, but only 8 name bytes before the directory ends */
    z.cd.p[z.cd.n - 8 - 46 + 28] = 250;
    zip_write(&z, apk);
    expect_rejected("i2-cd-name", apk, MODE_ORIGINAL);

    /* the same with a 46-byte header and nothing after it */
    path_of(apk, sizeof(apk), "i2-cd-header-only.apk");
    zip_add(&z, "assets/boz.s3e", 0, k_image_lzma, sizeof(k_image_lzma), sizeof(k_image_lzma));
    z.cd.n -= strlen("assets/boz.s3e");
    zip_write(&z, apk);
    expect_rejected("i2-cd-header-only", apk, MODE_ORIGINAL);
}

/* I7: an archive past 2 GiB (a sparse file: 2.5 GiB of hole, then the ZIP). On Windows, long is
 * 32 bits, so fseek/ftell could not reach it. Opt-in: CODBOZ_TEST_BIG=1. */
static void test_big_file(void) {
    const char *big = getenv("CODBOZ_TEST_BIG");
    if (!big || strcmp(big, "1") != 0) {
        printf("skipped: archive past 2 GiB (set CODBOZ_TEST_BIG=1)\n");
        return;
    }
    char apk[1024], root[1024], error[256] = "";
    path_of(apk, sizeof(apk), "big.apk");
    zip z = {0};
    z.base = 0xA0000000u;
    apk_entries(&z);
    zip_write(&z, apk);
    new_root(root, sizeof(root), "root-big");
    enum game_mode mode = MODE_COUNT;
    CHECK(install_package(apk, root, &mode, error, sizeof(error)), "APK past 2 GiB: %s", error);
    CHECK(file_equals(root, "original/assets/boz.s3e.unpacked", k_image, sizeof(k_image) - 1),
          "boz.s3e.unpacked from an APK past 2 GiB");
    remove(apk);
}

/* I3: sizes and offsets taken from the file must fit in the file before anything is allocated. */
static void test_sizes_past_the_file(void) {
    char apk[1024];
    zip z = {0};
    struct eocd_fields e = {0};

    path_of(apk, sizeof(apk), "i3-cd-size.apk");
    apk_entries(&z);
    e.cd_size = 0xfffffff0u; /* a 4 GiB central directory */
    zip_write_eocd(&z, apk, e);
    expect_rejected("i3-cd-size", apk, MODE_ORIGINAL);

    path_of(apk, sizeof(apk), "i3-cd-offset.apk");
    apk_entries(&z);
    e.cd_size = 0;
    e.cd_offset = 0x7ffffff0u; /* past the end of the file */
    zip_write_eocd(&z, apk, e);
    expect_rejected("i3-cd-offset", apk, MODE_ORIGINAL);

    path_of(apk, sizeof(apk), "i3-cd-count.apk");
    apk_entries(&z);
    e.cd_offset = 0;
    e.count = 0xffff; /* more entries than the directory holds: the real ones still install */
    zip_write_eocd(&z, apk, e);
    char root[1024], error[256] = "";
    new_root(root, sizeof(root), "root-i3-cd-count");
    enum game_mode mode = MODE_COUNT;
    CHECK(install_package(apk, root, &mode, error, sizeof(error)), "i3-cd-count: %s", error);

    /* stored entry claiming 3.75 GiB of data in a small file */
    path_of(apk, sizeof(apk), "i3-entry-size.apk");
    struct zip_fields huge = {0xf0000000u, 0xf0000000u, 0, 0};
    zip_add_fields(&z, "assets/boz.s3e", 0, k_image_lzma, sizeof(k_image_lzma), huge);
    zip_write(&z, apk);
    expect_rejected("i3-entry-size", apk, MODE_ORIGINAL);

    /* deflated entry claiming 2 GiB out of 44 bytes */
    path_of(apk, sizeof(apk), "i3-deflate-ratio.apk");
    zip_add(&z, "assets/boz.s3e", 8, k_text_deflate, sizeof(k_text_deflate), 0x7fff0000u);
    zip_write(&z, apk);
    expect_rejected("i3-deflate-ratio", apk, MODE_ORIGINAL);

    /* local header offset past the end of the file */
    path_of(apk, sizeof(apk), "i3-local-offset.apk");
    zip_add(&z, "assets/boz.s3e", 0, k_image_lzma, sizeof(k_image_lzma), sizeof(k_image_lzma));
    set32(z.cd.p + 42, 0x40000000u);
    zip_write(&z, apk);
    expect_rejected("i3-local-offset", apk, MODE_ORIGINAL);

    /* LZMA header asking for 256 MiB that the stream cannot fill */
    path_of(apk, sizeof(apk), "i3-lzma-size.apk");
    uint8_t lzma[sizeof(k_image_lzma)];
    memcpy(lzma, k_image_lzma, sizeof(lzma));
    set32(lzma + 5, 256u << 20);
    zip_add(&z, "assets/boz.s3e", 0, lzma, sizeof(lzma), sizeof(lzma));
    zip_write(&z, apk);
    expect_rejected("i3-lzma-size", apk, MODE_ORIGINAL);
}

/* I4: a deflated entry of size 0xffffffff, whose int cast is the decoder's -1 failure value. The
 * data is 4.2 MB of garbage so that the size passes the deflate ratio check. */
static void test_size_as_int(void) {
    char apk[1024];
    path_of(apk, sizeof(apk), "i4-size-minus-one.apk");
    size_t garbage_len = 4200000;
    uint8_t *garbage = malloc(garbage_len);
    for (size_t i = 0; i < garbage_len; ++i) {
        garbage[i] = 0xff; /* BTYPE 3: invalid block, the decoder fails at once */
    }
    zip z = {0};
    zip_add(&z, "assets/boz.s3e", 8, garbage, garbage_len, 0xffffffffu);
    zip_write(&z, apk);
    free(garbage);
    expect_rejected("i4-size-minus-one", apk, MODE_ORIGINAL);
}

/* I5: a load command whose size wraps pos + cmdsize around 32 bits. */
static void test_macho_cmdsize_wrap(void) {
    struct ios_fields f = {0};
    f.cmdsize = 0xfffffff0u;
    f.nsects = 0x03ffffffu;
    f.rename_sections = 1;
    ipa_expect_rejected("i5-cmdsize-wrap", f);
    struct ios_fields past = {0};
    past.cmdsize = IOS_EXE_SIZE; /* runs past the end of the executable */
    ipa_expect_rejected("i5-cmdsize-past", past);
}

/* I6: relocation offsets near 4 GiB (off + 4 wraps) and fixup records whose size wraps. */
static void test_ios_relocations(void) {
    struct ios_fields f = {0};
    f.reloc0 = 0xfffffffcu; /* a key probe */
    f.reloc0_set = 1;
    ipa_expect_rejected("i6-reloc-probe", f);
    struct ios_fields last = {0};
    last.reloc_last = 0xfffffffdu; /* only the final check reads it */
    ipa_expect_rejected("i6-reloc-check", last);
    struct ios_fields wrap = {0};
    wrap.fix_record_size = 0xfffffff8u; /* 76 + size wraps to 68, the record before: a loop */
    ipa_expect_rejected("i6-fixup-wrap", wrap);
}

/* I8: an install that fails halfway must not look complete, also over an earlier good install. */
static void test_install_is_atomic(void) {
    char apk[1024], good[1024], root[1024], error[256] = "";
    enum game_mode mode = MODE_COUNT;
    static const uint8_t bad_deflate[] = {0xff, 0xff, 0xff, 0xff}; /* BTYPE 3: invalid */
    zip z = {0};
    path_of(apk, sizeof(apk), "i8-broken-after-image.apk");
    apk_entries(&z); /* boz.s3e comes first and decodes fine */
    zip_add(&z, "assets/zz/broken.bin", 8, bad_deflate, sizeof(bad_deflate), 16);
    zip_write(&z, apk);
    expect_rejected("i8-broken-after-image", apk, MODE_ORIGINAL);
    CHECK(!file_exists(g_tmp, "root-i8-broken-after-image/original/assets/boz.s3e.unpacked") &&
              !file_exists(g_tmp, "root-i8-broken-after-image/original/assets/boz.s3e.unpacked.part"),
          "i8: the game image of a failed install is not left behind");

    /* a good install, then a failing reinstall over it */
    path_of(good, sizeof(good), "i8-good.apk");
    apk_entries(&z);
    zip_write(&z, good);
    new_root(root, sizeof(root), "root-i8-reinstall");
    CHECK(install_package(good, root, &mode, error, sizeof(error)), "i8 first install: %s", error);
    CHECK(install_package(good, root, &mode, error, sizeof(error)), "i8 reinstall: %s", error);
    CHECK(install_present(root, MODE_ORIGINAL), "i8: a reinstall over an install succeeds");
    CHECK(!file_exists(root, "original/assets/boz.s3e.unpacked.part") &&
              !file_exists(root, "original/assets/data/menu.txt.part"),
          "i8: no .part files after a good install");
    CHECK(!install_package(apk, root, &mode, error, sizeof(error)), "i8: broken reinstall accepted");
    CHECK(!install_present(root, MODE_ORIGINAL),
          "i8: a failed reinstall leaves the install marked complete");

    /* IPA: a good install, then a reinstall whose blackops_gles1.dz is broken */
    char ipa[1024], broken[1024];
    path_of(ipa, sizeof(ipa), "i8-good.ipa");
    path_of(broken, sizeof(broken), "i8-broken-gles1.ipa");
    struct ios_fields valid = {0};
    uint8_t *exe = ios_exe(valid, NULL, NULL);
    ipa_write(ipa, exe);
    zip_add(&z, "Payload/BOZ.app/BOZ", 0, exe, IOS_EXE_SIZE, IOS_EXE_SIZE);
    zip_add(&z, "Payload/BOZ.app/blackops_gles1.dz", 8, bad_deflate, sizeof(bad_deflate), 16);
    zip_write(&z, broken);
    free(exe);
    new_root(root, sizeof(root), "root-i8-ipa");
    CHECK(install_package(ipa, root, &mode, error, sizeof(error)), "i8 IPA install: %s", error);
    CHECK(!install_package(broken, root, &mode, error, sizeof(error)), "i8: broken IPA accepted");
    CHECK(!install_present(root, MODE_IOS),
          "i8: a failed IPA reinstall leaves the install marked complete");
}

/* Archives that are simply not the game, rejected by the original installer too. */
static void test_not_the_game(void) {
    char path[1024];
    path_of(path, sizeof(path), "noise.bin");
    FILE *f = fopen(path, "wb");
    for (int i = 0; i < 5000; ++i) {
        fputc((i * 131 + 7) & 0xff, f);
    }
    fclose(f);
    expect_rejected("noise", path, MODE_ORIGINAL);
    path_of(path, sizeof(path), "no-image.apk");
    zip z = {0};
    zip_add(&z, "assets/data/menu.txt", 0, "menu", 4, 4);
    zip_write(&z, path);
    expect_rejected("no-image", path, MODE_ORIGINAL);
    struct ios_fields renamed = {0};
    renamed.rename_sections = 1; /* no Marmalade sections in the executable */
    ipa_expect_rejected("no-marmalade", renamed);
}

int main(void) {
    const char *base = getenv("CODBOZ_TEST_TMP"); /* under Wine, TMPDIR belongs to wineserver */
    if (!base || !base[0]) {
        base = getenv("TMPDIR");
    }
    snprintf(g_tmp, sizeof(g_tmp), "%s/codboz-test-install-XXXXXX", base && base[0] ? base : "/tmp");
#ifdef _WIN32
    if (!_mktemp(g_tmp) || _mkdir(g_tmp) != 0) {
#else
    if (!mkdtemp(g_tmp)) {
#endif
        printf("cannot create a scratch directory\n");
        return 2;
    }
    test_apk_original();
    test_apk_reimagined();
    test_ipa();
    test_not_the_game();
    test_stored_size_mismatch();
    test_cd_name_overflow();
    test_big_file();
    test_sizes_past_the_file();
    test_size_as_int();
    test_macho_cmdsize_wrap();
    test_ios_relocations();
    test_install_is_atomic();
    printf("%s: %d checks, %d failed (scratch files in %s)\n", fails ? "FAIL" : "ok", checks, fails,
           g_tmp);
    return fails ? 1 : 0;
}
