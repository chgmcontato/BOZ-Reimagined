/* Mutation fuzzer for the installer (src/install.c), with no fuzzing library: it mutates seed
 * archives (APK/IPA) and runs install_package on each, under ASan/UBSan.
 *   cc -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc tools/fuzz_install.c \
 *      src/install.c third_party/LzmaDec.c -lm -o /tmp/fuzz_install
 *   /tmp/fuzz_install <seconds> <scratch dir> <seed.apk|seed.ipa>...
 * Seeds: the synthetic archives tools/test_install leaves in its scratch directory
 * (i8-good.apk, reimagined.apk, boz.ipa), or your own copies. Mutations favour the Mach-O header
 * and load commands, and the ZIP central directory and end record. CODBOZ_FUZZ_SEED picks the
 * random sequence (default 12345), so a run can be repeated. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../third_party/stb_image.h"

#include "install.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* allocations above 512 MiB fail, as on a phone without overcommit */
const char *__asan_default_options(void);
const char *__asan_default_options(void) {
    return "allocator_may_return_null=1:max_allocation_size_mb=512";
}

enum { MAX_SEEDS = 16 };

static uint8_t *slurp(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    uint8_t *data = NULL;
    size_t n = 0, cap = 0, got;
    uint8_t chunk[65536];
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (n + got > cap) {
            cap = (n + got) * 2;
            data = realloc(data, cap);
        }
        memcpy(data + n, chunk, got);
        n += got;
    }
    fclose(f);
    *size = n;
    return data;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <seconds> <scratch dir> <seed>...\n", argv[0]);
        return 2;
    }
    int seconds = atoi(argv[1]);
    const char *dir = argv[2];
    int seeds = 0;
    uint8_t *seed[MAX_SEEDS];
    size_t seed_size[MAX_SEEDS], macho_at[MAX_SEEDS], largest = 0;
    for (int i = 3; i < argc && seeds < MAX_SEEDS; ++i) {
        seed[seeds] = slurp(argv[i], &seed_size[seeds]);
        if (!seed[seeds] || seed_size[seeds] < 64) {
            fprintf(stderr, "cannot read seed %s\n", argv[i]);
            return 2;
        }
        macho_at[seeds] = 0;
        for (size_t at = 0; at + 4 <= seed_size[seeds]; ++at) {
            if (memcmp(seed[seeds] + at, "\xce\xfa\xed\xfe", 4) == 0) {
                macho_at[seeds] = at;
                break;
            }
        }
        if (seed_size[seeds] > largest) {
            largest = seed_size[seeds];
        }
        seeds++;
    }
    const char *seed_env = getenv("CODBOZ_FUZZ_SEED");
    srand(seed_env ? (unsigned)strtoul(seed_env, NULL, 10) : 12345u);

    char path[2048], root[2048];
    snprintf(path, sizeof(path), "%s/fuzz-input.bin", dir);
    snprintf(root, sizeof(root), "%s/fuzz-root", dir);
    uint8_t *b = malloc(largest);
    long runs = 0, accepted = 0;
    time_t end = time(NULL) + seconds;
    while (time(NULL) < end) {
        int s = (int)(runs % seeds);
        size_t len = seed_size[s];
        memcpy(b, seed[s], len);
        int mutations = 1 + rand() % 6;
        for (int k = 0; k < mutations; ++k) {
            size_t at;
            int region = rand() % 3;
            if (macho_at[s] && region == 0) {
                at = macho_at[s] + (size_t)rand() % 1200; /* Mach-O header, commands, S3E header */
            } else if (region == 1) {
                at = len - 1 - (size_t)rand() % (len < 600 ? len : 600); /* directory + end */
            } else {
                at = (size_t)rand() % len;
            }
            if (at >= len) {
                at = len - 1;
            }
            switch (rand() % 4) {
            case 0:
                b[at] = (uint8_t)rand();
                break;
            case 1:
                b[at] ^= (uint8_t)(1u << (rand() % 8));
                break;
            case 2:
                if (at + 4 <= len) {
                    uint32_t v = rand() % 5 == 0 ? 0xffffffffu - (uint32_t)(rand() % 8)
                                                 : (uint32_t)rand();
                    memcpy(b + at, &v, 4);
                }
                break;
            default:
                if (len > 64) {
                    len = 22 + (size_t)rand() % (len - 22); /* truncate */
                }
                break;
            }
        }
        FILE *f = fopen(path, "wb");
        if (!f) {
            fprintf(stderr, "cannot write %s\n", path);
            return 2;
        }
        fwrite(b, 1, len, f);
        fclose(f);
        enum game_mode mode;
        char error[256] = "";
        accepted += install_package(path, root, &mode, error, sizeof(error));
        runs++;
    }
    free(b);
    for (int i = 0; i < seeds; ++i) {
        free(seed[i]);
    }
    fprintf(stderr, "fuzz_install: %ld archives in %d s, %ld accepted, no sanitizer errors\n", runs,
            seconds, accepted);
    return 0;
}
