/*
 * OpenGL ES builds only (Android). The game sends each shader as one string per line, 165 and 182
 * of them for its main vertex and fragment shaders, so the number of fragments cannot be bounded.
 * They become one buffer, as on the PC, and that exact text can be kept on disk for diagnosis.
 * No dependency on the runtime: tools/test_gles_shaders.c links this file alone.
 */
#include "gles_shader.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { DUMP_MAX_FILES = 512 };
#define DUMP_MAX_BYTES (64u << 20)

char *gles_shader_join(int32_t count, const char *const *strings, const int32_t *lengths, size_t *size) {
    size_t total = 0;
    if (count < 0 || (count > 0 && !strings)) {
        return NULL;
    }
    for (int32_t i = 0; i < count; ++i) {
        if (!strings[i]) {
            return NULL; /* GL leaves this undefined; refusing beats reading guest address 0 */
        }
        size_t len = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
        if (len > SIZE_MAX - total - 1) {
            return NULL;
        }
        total += len;
    }
    char *text = malloc(total + 1);
    if (!text) {
        return NULL;
    }
    size_t at = 0;
    for (int32_t i = 0; i < count; ++i) {
        size_t len = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
        memcpy(text + at, strings[i], len);
        at += len;
    }
    text[at] = 0;
    if (size) {
        *size = at;
    }
    return text;
}

uint64_t gles_shader_hash(const char *text, size_t size) {
    uint64_t hash = 0xcbf29ce484222325ull; /* FNV-1a */
    for (size_t i = 0; i < size; ++i) {
        hash = (hash ^ (uint8_t)text[i]) * 0x100000001b3ull;
    }
    return hash;
}

int gles_shader_dump(const char *dir, uint64_t hash, const char *text, size_t size, char *path,
                     size_t path_size) {
    static unsigned files;
    static size_t bytes;
    int written = snprintf(path, path_size, "%s/%016llx.glsl", dir, (unsigned long long)hash);
    if (written < 0 || (size_t)written >= path_size) {
        return 0;
    }
    struct stat st;
    if (stat(path, &st) == 0 && (size_t)st.st_size == size) {
        return 1;
    }
    if (files >= DUMP_MAX_FILES || size > DUMP_MAX_BYTES - bytes) {
        return 0;
    }
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        return 0;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        return 0;
    }
    size_t done = fwrite(text, 1, size, f);
    int closed = fclose(f);
    if (done != size || closed != 0) {
        remove(path); /* a partial file would look like a truncated shader */
        return 0;
    }
    files++;
    bytes += size;
    return 1;
}
