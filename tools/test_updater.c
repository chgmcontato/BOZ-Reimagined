/* Updater checks (src/updater.c).
 *   cc -O2 -Isrc tools/test_updater.c src/updater.c $(pkg-config --cflags --libs sdl2) -lm -o /tmp/t
 *   /tmp/t                              offline checks only
 *   /tmp/t <release.zip> <release.tar.gz> [SHA256SUMS.txt]   + the real archive layouts
 *   CODBOZ_TEST_NETWORK=1 /tmp/t ...    + a real update from GitHub into a scratch file
 * Windows (run under Wine or on Windows):
 *   x86_64-w64-mingw32-gcc -O2 -Isrc -I$SDL/include/SDL2 tools/test_updater.c src/updater.c
 *     -L$SDL/lib -lmingw32 -lSDL2 -lwinhttp -lsetupapi -limm32 -lversion -lwinmm -lole32 -loleaut32
 *     -lgdi32 -luuid -lcfgmgr32 -static -o test_updater.exe     ($SDL: build-win/deps/SDL2-<v>/x86_64-w64-mingw32)
 */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../third_party/stb_image.h"
#include "updater.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t *slurp(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n + 1);
    *size = fread(buf, 1, (size_t)n, f);
    buf[*size] = 0;
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    /* versions */
    CHECK(updater_compare_versions("v0.5.2", "0.5.1") > 0, "0.5.2 > 0.5.1");
    CHECK(updater_compare_versions("v0.5", "0.5.1") < 0, "0.5 < 0.5.1");
    CHECK(updater_compare_versions("v0.5.1", "0.5.1") == 0, "equal");
    CHECK(updater_compare_versions("v0.10", "0.9.9") > 0, "0.10 > 0.9.9");
    CHECK(updater_compare_versions("v1.0", "0.99") > 0, "1.0 > 0.99");
    CHECK(updater_compare_versions("v0.5", "0.5.0") == 0, "0.5 == 0.5.0");

    /* SHA-256 (FIPS 180-2 vectors; the 56-byte one needs a second padding block) */
    char hex[65];
    updater_sha256_hex("", 0, hex);
    CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0, "empty %s", hex);
    updater_sha256_hex("abc", 3, hex);
    CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0, "abc %s", hex);
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    updater_sha256_hex(two, strlen(two), hex);
    CHECK(strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0, "448 bits %s", hex);
    char *million = malloc(1000000);
    memset(million, 'a', 1000000);
    updater_sha256_hex(million, 1000000, hex);
    CHECK(strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0, "1M a %s", hex);
    free(million);

    /* SHA256SUMS.txt */
    const char *sums =
        "c22f17a095ab63410e8bda4aff5ad3ef5af9796e86c6edbf293f011d3074c25e  boz-reimagined-tv-v0.4.xdelta\n"
        "9e379fd8b66bc0b79916d452a5a80d0f77c42b262c73c6f608c94e95e28a429d  codboz-pc-linux-x86_64.tar.gz\r\n"
        "F33D92FDF61AE7420EFE79D308F98A83642ABF7CA876CEB8E939764082184F86 *codboz-pc-windows-x64.zip";
    CHECK(updater_find_sum(sums, "codboz-pc-linux-x86_64.tar.gz", hex) &&
          strcmp(hex, "9e379fd8b66bc0b79916d452a5a80d0f77c42b262c73c6f608c94e95e28a429d") == 0, "linux sum");
    CHECK(updater_find_sum(sums, "codboz-pc-windows-x64.zip", hex) &&
          strcmp(hex, "f33d92fdf61ae7420efe79d308f98a83642abf7ca876ceb8e939764082184f86") == 0, "windows sum, binary mode");
    CHECK(!updater_find_sum(sums, "codboz-pc-windows-x64", hex), "prefix must not match");
    CHECK(!updater_find_sum(sums, "x.zip", hex), "missing");

    /* garbage never extracts */
    uint8_t junk[600] = {0x1f, 0x8b, 8};
    size_t n = 0;
    CHECK(!updater_extract(junk, sizeof junk, false, "codboz-pc", &n), "junk tar.gz");
    CHECK(!updater_extract(junk, sizeof junk, true, "codboz-pc.exe", &n), "junk zip");

    /* replacing a file in place */
    const char *scratch = "/tmp/codboz-updater-test.bin";
    FILE *f = fopen(scratch, "wb");
    fputs("old", f);
    fclose(f);
    char err[256];
    CHECK(updater_replace_exe(scratch, (const uint8_t *)"new contents", 12, err, sizeof err), "replace: %s", err);
    uint8_t *back = slurp(scratch, &n);
    CHECK(back && n == 12 && memcmp(back, "new contents", 12) == 0, "replaced contents");
    free(back);
    CHECK(!updater_replace_exe("/nonexistent-dir/codboz-pc", (const uint8_t *)"x", 1, err, sizeof err), "unwritable dir");

    /* the real release archives */
    if (argc >= 3) {
        size_t zs, ts;
        uint8_t *zip = slurp(argv[1], &zs), *tgz = slurp(argv[2], &ts);
        size_t es = 0;
        uint8_t *exe = zip ? updater_extract(zip, zs, true, "codboz-pc.exe", &es) : NULL;
        CHECK(exe && es > 1000000 && exe[0] == 'M' && exe[1] == 'Z', "exe from zip (%zu bytes)", es);
        free(exe);
        exe = tgz ? updater_extract(tgz, ts, false, "codboz-pc", &es) : NULL;
        CHECK(exe && es > 1000000 && memcmp(exe, "\177ELF", 4) == 0, "ELF from tar.gz (%zu bytes)", es);
        free(exe);
        CHECK(!updater_extract(tgz, ts, false, "LEIA", &es), "partial names do not match");
        if (argc >= 4) {
            size_t ss;
            char *s = (char *)slurp(argv[3], &ss), want[65];
            updater_sha256_hex(zip, zs, hex);
            CHECK(s && updater_find_sum(s, "codboz-pc-windows-x64.zip", want) && strcmp(hex, want) == 0, "zip hash");
            updater_sha256_hex(tgz, ts, hex);
            CHECK(s && updater_find_sum(s, "codboz-pc-linux-x86_64.tar.gz", want) && strcmp(hex, want) == 0, "tar.gz hash");
            free(s);
        }
        free(zip);
        free(tgz);
    }

    /* a real update from GitHub (Linux: needs curl and network) */
    if (getenv("CODBOZ_TEST_NETWORK")) {
        char version[32];
        f = fopen(scratch, "wb");
        fputs("old", f);
        fclose(f);
        bool ok = updater_run("0.0.1", scratch, version, sizeof version, err, sizeof err);
        CHECK(ok && version[0], "update from 0.0.1: %s", err);
        back = slurp(scratch, &n);
#ifdef _WIN32
        CHECK(back && n > 1000000 && memcmp(back, "MZ", 2) == 0, "new executable in place (%zu bytes)", n);
#else
        CHECK(back && n > 1000000 && memcmp(back, "\177ELF", 4) == 0, "new executable in place (%zu bytes)", n);
#endif
        free(back);
        printf("latest release: %s\n", version);
        char latest[32];
        snprintf(latest, sizeof latest, "%s", version);
        ok = updater_run(latest, scratch, version, sizeof version, err, sizeof err);
        CHECK(ok && !version[0], "already current: %s", err);
    }
    remove(scratch);

    printf(fails ? "%d FAILED\n" : "updater: all checks passed\n", fails);
    return fails != 0;
}
