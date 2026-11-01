#ifndef CODBOZ_PC_GL_STATE_H
#define CODBOZ_PC_GL_STATE_H

#include <stdbool.h>
#include <stdint.h>

enum gl_state_operation {
    GL_STATE_BIND_BUFFER, GL_STATE_BIND_TEXTURE, GL_STATE_USE_PROGRAM,
    GL_STATE_ACTIVE_TEXTURE, GL_STATE_ATTRIB_POINTER, GL_STATE_OPERATION_COUNT
};

void gl_state_init(bool enabled, unsigned texture_units, unsigned attributes);
/* The host uses a compatibility context; client arrays require VAO zero. */
void gl_state_client_arrays(bool enabled);
void gl_state_vertex_array(uint32_t vao);
/* Host UI/context changes bypass the guest thunks. Resource identities survive. */
void gl_state_invalidate(void);
void gl_state_reset_counts(void);
void gl_state_report(double seconds);

/* True means forward the call; false means a known identical state is active. */
bool gl_state_bind_buffer(uint32_t target, uint32_t buffer);
bool gl_state_bind_texture(uint32_t target, uint32_t texture);
bool gl_state_use_program(uint32_t program);
bool gl_state_active_texture(uint32_t texture);
bool gl_state_attrib_pointer(uint32_t index, int32_t size, uint32_t type,
                             uint32_t normalized, int32_t stride, uintptr_t pointer);

void gl_state_textures_created(int32_t count, const uint32_t *names);
void gl_state_textures_deleted(int32_t count, const uint32_t *names);
void gl_state_program_status(uint32_t program, uint32_t pname, const int32_t *value);
void gl_state_program_changed(uint32_t program);

#endif
