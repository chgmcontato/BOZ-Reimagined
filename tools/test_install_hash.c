/* boz.s3e fingerprint: install_apk records the SHA-256 of the APK's boz.s3e next to it, and
 * install_image_reference() tells the Android 1.0.11 reference build from any other (computing
 * the hash once for installs made before it was recorded). Uses a made-up APK, no game data. */
#include <assert.h>
#include <unistd.h>

#include "../src/install.c"

/* install.c and updater.c inflate through stb_image's zlib decoder */
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

int g_log_verbose;

/* lzma.compress(b"BOZ-TEST-S3E image bytes", format=lzma.FORMAT_ALONE), with the unknown size in
 * the header replaced by the real one (24), as the game's boz.s3e has it */
static const uint8_t k_s3e[] = {
    0x5d, 0x00, 0x00, 0x80, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21,
    0x13, 0xc7, 0x98, 0x02, 0xfd, 0xb9, 0x05, 0x7f, 0xde, 0xd2, 0x9f, 0xcf, 0xc4, 0x8b, 0xdb,
    0x0b, 0x9a, 0x39, 0x81, 0x8c, 0x33, 0x8e, 0x71, 0x65, 0x77, 0xc4, 0x25, 0xf3, 0xff, 0xff,
    0xc8, 0xf7, 0x00, 0x00,
};
static const char k_unpacked[] = "BOZ-TEST-S3E image bytes";

static void put16(FILE *f, unsigned v) {
    fputc((int)(v & 0xff), f);
    fputc((int)(v >> 8), f);
}

static void put32(FILE *f, uint32_t v) {
    put16(f, v & 0xffffu);
    put16(f, v >> 16);
}

/* A zip with stored entries, as much of the format as read_central_directory needs. */
static void write_apk(const char *path) {
    struct {
        const char *name;
        const void *data;
        uint32_t size, offset;
    } entries[] = {
        {"assets/boz.s3e", k_s3e, sizeof(k_s3e), 0},
        {"assets/readme.txt", "not the game\n", 13, 0},
    };
    FILE *f = fopen(path, "wb");
    assert(f);
    for (size_t i = 0; i < 2; ++i) {
        entries[i].offset = (uint32_t)ftell(f);
        put32(f, 0x04034b50u);
        put16(f, 20), put16(f, 0), put16(f, 0), put16(f, 0), put16(f, 0);
        put32(f, 0), put32(f, entries[i].size), put32(f, entries[i].size);
        put16(f, (unsigned)strlen(entries[i].name)), put16(f, 0);
        fputs(entries[i].name, f);
        fwrite(entries[i].data, 1, entries[i].size, f);
    }
    uint32_t cd = (uint32_t)ftell(f);
    for (size_t i = 0; i < 2; ++i) {
        put32(f, 0x02014b50u);
        put16(f, 20), put16(f, 20), put16(f, 0), put16(f, 0), put16(f, 0), put16(f, 0);
        put32(f, 0), put32(f, entries[i].size), put32(f, entries[i].size);
        put16(f, (unsigned)strlen(entries[i].name)), put16(f, 0), put16(f, 0), put16(f, 0),
            put16(f, 0);
        put32(f, 0), put32(f, entries[i].offset);
        fputs(entries[i].name, f);
    }
    uint32_t cd_size = (uint32_t)ftell(f) - cd;
    put32(f, 0x06054b50u);
    put16(f, 0), put16(f, 0), put16(f, 2), put16(f, 2);
    put32(f, cd_size), put32(f, cd), put16(f, 0);
    fclose(f);
}

static void read_text(const char *path, char *out, size_t size) {
    FILE *f = fopen(path, "rb");
    assert(f);
    size_t n = fread(out, 1, size - 1, f);
    out[n] = 0;
    fclose(f);
}

static void write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    assert(f);
    fputs(text, f);
    fclose(f);
}

int main(void) {
    char root[] = "/tmp/codboz-install-hash-XXXXXX";
    assert(mkdtemp(root));
    char apk[1200], assets[1200], hash_file[1300], s3e[1300], unpacked[1300], readme[1300];
    snprintf(apk, sizeof(apk), "%s/test.apk", root);
    snprintf(assets, sizeof(assets), "%s/original/assets", root);
    snprintf(hash_file, sizeof(hash_file), "%s/boz.s3e.sha256", assets);
    snprintf(s3e, sizeof(s3e), "%s/boz.s3e", assets);
    snprintf(unpacked, sizeof(unpacked), "%s/boz.s3e.unpacked", assets);
    snprintf(readme, sizeof(readme), "%s/readme.txt", assets);
    write_apk(apk);

    /* not installed yet */
    assert(install_image_reference(root, MODE_ORIGINAL) == -1);
    g_image_reference[MODE_ORIGINAL] = -2;

    enum game_mode mode = MODE_COUNT;
    char error[256] = "";
    if (install_apk(apk, root, &mode, error, sizeof(error)) != 1) {
        fprintf(stderr, "FAIL install_apk: %s\n", error);
        return 1;
    }
    assert(mode == MODE_ORIGINAL);

    char expected[65], line[128];
    updater_sha256_hex(k_s3e, sizeof(k_s3e), expected);
    read_text(hash_file, line, sizeof(line));
    assert(strncmp(line, expected, 64) == 0 && line[64] == '\n');
    read_text(unpacked, line, sizeof(line));
    assert(strcmp(line, k_unpacked) == 0);
    assert(install_image_reference(root, MODE_ORIGINAL) == 0); /* recorded at install */

    /* the reference hash on record */
    write_text(hash_file, INSTALL_ORIGINAL_S3E_SHA256 "\n");
    g_image_reference[MODE_ORIGINAL] = -2;
    assert(install_image_reference(root, MODE_ORIGINAL) == 1);

    /* installed by an older version: no record, hashed once from boz.s3e */
    unlink(hash_file);
    g_image_reference[MODE_ORIGINAL] = -2;
    assert(install_image_reference(root, MODE_ORIGINAL) == 0);
    read_text(hash_file, line, sizeof(line));
    assert(strncmp(line, expected, 64) == 0);

    /* no reference for the patched TV build or the iOS executable */
    assert(install_image_reference(root, MODE_REIMAGINED) == -1);
    assert(install_image_reference(root, MODE_IOS) == -1);

    unlink(hash_file), unlink(s3e), unlink(unpacked), unlink(readme), unlink(apk);
    rmdir(assets);
    snprintf(line, sizeof(line), "%s/original", root);
    rmdir(line);
    rmdir(root);
    printf("install hash: recorded %.12s..., reference and older installs recognised\n", expected);
    return 0;
}
