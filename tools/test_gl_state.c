#include <assert.h>
#include <stdio.h>
#include "../src/gl_state.h"

int main(void) {
    const uint32_t array = 0x8892, element = 0x8893, tex2d = 0x0de1;
    const uint32_t cube = 0x8513, unit0 = 0x84c0;
    uint32_t textures[] = {11, 12};
    int32_t linked = 1;
    gl_state_init(true, 8, 16);
    assert(gl_state_bind_buffer(array, 7));
    assert(!gl_state_bind_buffer(array, 7));
    assert(gl_state_bind_buffer(element, 7)); /* Targets have independent state. */
    assert(!gl_state_bind_buffer(element, 7));
    assert(gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 0));
    assert(!gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 0));
    assert(gl_state_bind_buffer(array, 8));
    assert(gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 0)); /* Same offset, different VBO. */
    assert(gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 4));
    assert(gl_state_attrib_pointer(16, 3, 0x1406, 0, 12, 0)); /* Out-of-range goes to GL. */
    assert(gl_state_attrib_pointer(16, 3, 0x1406, 0, 12, 0));
    assert(gl_state_bind_buffer(array, 0));
    assert(gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 0)); /* Client arrays bypass. */
    assert(gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 0));

    gl_state_client_arrays(true);
    assert(gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 0x30000000));
    assert(!gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 0x30000000));
    assert(gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 0x30000010));
    assert(gl_state_attrib_pointer(0, 2, 0x1406, 0, 12, 0x30000010));
    assert(gl_state_bind_buffer(array, 8));
    assert(gl_state_attrib_pointer(0, 2, 0x1406, 0, 12, 0x30000010));
    assert(gl_state_bind_buffer(array, 0));
    assert(gl_state_attrib_pointer(0, 2, 0x1406, 0, 12, 0x30000010));
    gl_state_vertex_array(4);
    assert(gl_state_bind_buffer(array, 0));
    assert(gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 0x30000000));
    assert(gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 0x30000000));
    gl_state_vertex_array(0);
    assert(gl_state_bind_buffer(array, 0));
    assert(gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 0x30000000));
    assert(!gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 0x30000000));
    gl_state_invalidate();
    assert(gl_state_bind_buffer(array, 0));
    assert(gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 0x30000000));
    gl_state_client_arrays(false);
    assert(gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 0x30000000));
    assert(gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 0x30000000));

    gl_state_textures_created(2, textures);
    assert(gl_state_active_texture(unit0));
    assert(!gl_state_active_texture(unit0));
    assert(gl_state_bind_texture(tex2d, 11));
    assert(!gl_state_bind_texture(tex2d, 11));
    assert(gl_state_active_texture(unit0 + 1));
    assert(gl_state_bind_texture(tex2d, 11)); /* Same texture on another unit still binds. */
    assert(!gl_state_bind_texture(tex2d, 11));
    assert(gl_state_bind_texture(cube, 11)); /* Invalid object target is never suppressed. */
    assert(gl_state_bind_texture(cube, 11));
    assert(gl_state_bind_texture(cube, 12));
    assert(!gl_state_bind_texture(cube, 12));
    assert(gl_state_active_texture(0xffffffffu));
    assert(gl_state_bind_texture(tex2d, 11));
    assert(gl_state_active_texture(unit0));
    assert(gl_state_bind_texture(tex2d, 11)); /* Unknown active unit invalidates affected knowledge. */
    gl_state_textures_deleted(1, textures);
    assert(gl_state_active_texture(unit0));
    assert(gl_state_bind_texture(tex2d, 11));
    assert(gl_state_bind_texture(tex2d, 11)); /* Deleted name is no longer cacheable. */
    gl_state_textures_created(1, textures);
    assert(gl_state_bind_texture(tex2d, 11));
    assert(!gl_state_bind_texture(tex2d, 11));

    assert(gl_state_use_program(0));
    assert(!gl_state_use_program(0));
    assert(gl_state_use_program(99));
    assert(gl_state_use_program(99)); /* Never suppress unknown/unlinked program errors. */
    gl_state_program_status(99, 0x8b82, &linked);
    assert(gl_state_use_program(99));
    assert(!gl_state_use_program(99));
    gl_state_program_changed(99);
    assert(gl_state_use_program(99));
    gl_state_program_status(99, 0x8b82, &linked);
    assert(gl_state_use_program(99));
    assert(!gl_state_use_program(99));

    gl_state_invalidate(); /* HUD, context, VAO switch, or object deletion. */
    assert(gl_state_use_program(99)); /* Program identity survives host UI, binding does not. */
    assert(gl_state_bind_buffer(array, 8));
    assert(gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 4));
    assert(gl_state_active_texture(unit0));
    assert(gl_state_bind_texture(tex2d, 11));

    gl_state_init(false, 8, 16);
    for (int i = 0; i < 3; ++i) {
        assert(gl_state_bind_buffer(array, 8));
        assert(gl_state_active_texture(unit0));
        assert(gl_state_use_program(0));
        assert(gl_state_attrib_pointer(0, 3, 0x1406, 0, 12, 4));
    }
    puts("PASS: GL cache redundancy; client arrays/VAO zero; pointer/format/buffer changes; VBO capture; object lifecycle; host invalidation; rollback");
}
