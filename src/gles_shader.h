/* OpenGL ES builds only: the game's shader text as the driver receives it. See gles_shader.c. */
#ifndef CODBOZ_GLES_SHADER_H
#define CODBOZ_GLES_SHADER_H

#include <stddef.h>
#include <stdint.h>

/* glShaderSource semantics: lengths may be NULL, a negative length means NUL-terminated.
 * Returns a malloc'ed, NUL-terminated buffer with every fragment in order, or NULL. */
char *gles_shader_join(int32_t count, const char *const *strings, const int32_t *lengths, size_t *size);

uint64_t gles_shader_hash(const char *text, size_t size);

/* Writes text to <dir>/<hash>.glsl, whole, once per distinct text. At most max_files files and
 * max_bytes bytes per directory run: a shader over the limit is refused, never cut.
 * Returns 1 when the file exists afterwards, 0 otherwise. */
int gles_shader_dump(const char *dir, uint64_t hash, const char *text, size_t size, char *path,
                     size_t path_size);

#endif
