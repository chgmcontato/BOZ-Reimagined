/* Conservative cache for the legacy guest's GL state. No allocations or GL
 * queries on the hot path. Unsupported/unknown state always goes to the driver. */
#include "gl_state.h"
#include "log.h"

#include <string.h>

enum { MAX_UNITS = 32, MAX_ATTRIBUTES = 32, TEXTURE_SLOTS = 4096, PROGRAM_SLOTS = 512 };
enum { ARRAY_BUFFER = 0x8892, ELEMENT_BUFFER = 0x8893, TEXTURE0 = 0x84c0,
       TEXTURE_2D = 0x0de1, TEXTURE_CUBE = 0x8513, LINK_STATUS = 0x8b82 };

struct binding { uint32_t value; bool known; };
struct attribute {
    uint32_t buffer, type, normalized;
    int32_t size, stride;
    uintptr_t pointer;
    bool known;
};
struct texture_identity { uint32_t name, target; };
struct program_identity { uint32_t name; bool linked; };
struct operation_count { uint64_t requested, forwarded; };

static bool g_enabled;
static bool g_client_arrays, g_default_vao;
static unsigned g_units, g_attributes;
static struct binding g_buffers[2], g_active, g_program;
static struct binding g_textures[MAX_UNITS][2];
static struct attribute g_attribs[MAX_ATTRIBUTES];
/* Direct-mapped identities: collisions simply make an old object uncacheable. */
static struct texture_identity g_texture_names[TEXTURE_SLOTS];
static struct program_identity g_program_names[PROGRAM_SLOTS];
static struct operation_count g_counts[GL_STATE_OPERATION_COUNT];
static struct {
    uint64_t client, vbo, unknown, changed, first, bypass;
    uintptr_t client_pointer;
} g_attrib_counts;

void gl_state_invalidate(void) {
    memset(g_buffers, 0, sizeof(g_buffers));
    memset(g_textures, 0, sizeof(g_textures));
    memset(g_attribs, 0, sizeof(g_attribs));
    g_active.known = g_program.known = false;
}

void gl_state_reset_counts(void) {
    memset(g_counts, 0, sizeof(g_counts));
    memset(&g_attrib_counts, 0, sizeof(g_attrib_counts));
}

void gl_state_init(bool enabled, unsigned texture_units, unsigned attributes) {
    g_enabled = enabled;
    g_client_arrays = false;
    g_default_vao = true; /* A freshly created native context starts at VAO zero. */
    g_units = texture_units < MAX_UNITS ? texture_units : MAX_UNITS;
    g_attributes = attributes < MAX_ATTRIBUTES ? attributes : MAX_ATTRIBUTES;
    memset(g_texture_names, 0, sizeof(g_texture_names));
    memset(g_program_names, 0, sizeof(g_program_names));
    gl_state_invalidate();
    gl_state_reset_counts();
}

void gl_state_client_arrays(bool enabled) {
    g_client_arrays = enabled;
    memset(g_attribs, 0, sizeof(g_attribs));
}

void gl_state_vertex_array(uint32_t vao) {
    gl_state_invalidate();
    /* Binding zero is always valid. Unknown/nonzero VAOs conservatively disable
     * the client-array cache, even if a bad bind left VAO zero active. Native
     * HUD drawing saves/restores the VAO, so ordinary invalidation retains this. */
    g_default_vao = vao == 0;
}

static bool forward(enum gl_state_operation op, bool redundant) {
    ++g_counts[op].requested;
    if (g_enabled && redundant) return false;
    ++g_counts[op].forwarded;
    return true;
}

static bool set_binding(enum gl_state_operation op, struct binding *entry, uint32_t value) {
    bool redundant = entry->known && entry->value == value;
    entry->value = value;
    entry->known = true;
    return forward(op, redundant);
}

bool gl_state_bind_buffer(uint32_t target, uint32_t buffer) {
    int slot = target == ARRAY_BUFFER ? 0 : target == ELEMENT_BUFFER ? 1 : -1;
    if (slot < 0) return forward(GL_STATE_BIND_BUFFER, false);
    return set_binding(GL_STATE_BIND_BUFFER, &g_buffers[slot], buffer);
}

bool gl_state_active_texture(uint32_t texture) {
    if (texture < TEXTURE0 || texture - TEXTURE0 >= g_units) {
        g_active.known = false;
        return forward(GL_STATE_ACTIVE_TEXTURE, false);
    }
    return set_binding(GL_STATE_ACTIVE_TEXTURE, &g_active, texture - TEXTURE0);
}

bool gl_state_bind_texture(uint32_t target, uint32_t texture) {
    int slot = target == TEXTURE_2D ? 0 : target == TEXTURE_CUBE ? 1 : -1;
    if (slot < 0) return forward(GL_STATE_BIND_TEXTURE, false);
    if (!g_active.known) {
        memset(g_textures, 0, sizeof(g_textures));
        return forward(GL_STATE_BIND_TEXTURE, false);
    }
    struct binding *binding = &g_textures[g_active.value][slot];
    if (texture) {
        struct texture_identity *id = &g_texture_names[texture % TEXTURE_SLOTS];
        /* Only guest-generated texture names with a compatible object target
         * are cached. Unknown names and target errors remain driver-visible. */
        if (id->name != texture || (id->target && id->target != target)) {
            binding->known = false;
            return forward(GL_STATE_BIND_TEXTURE, false);
        }
        id->target = target;
    }
    return set_binding(GL_STATE_BIND_TEXTURE, binding, texture);
}

bool gl_state_use_program(uint32_t program) {
    struct program_identity *id = &g_program_names[program % PROGRAM_SLOTS];
    if (program && (id->name != program || !id->linked)) {
        g_program.known = false;
        return forward(GL_STATE_USE_PROGRAM, false);
    }
    return set_binding(GL_STATE_USE_PROGRAM, &g_program, program);
}

bool gl_state_attrib_pointer(uint32_t index, int32_t size, uint32_t type,
                             uint32_t normalized, int32_t stride, uintptr_t pointer) {
    if (!g_buffers[0].known) ++g_attrib_counts.unknown;
    else if (g_buffers[0].value) ++g_attrib_counts.vbo;
    else {
        ++g_attrib_counts.client;
        g_attrib_counts.client_pointer = pointer;
    }
    bool type_ok = type == 0x1406 || type == 0x1400 || type == 0x1401 ||
                   type == 0x1402 || type == 0x1403; /* float, [u]byte, [u]short */
    /* A descriptor captures buffer + pointer. With buffer zero, pointer is an
     * identity-mapped host address. Its contents may change between draws: GL
     * reads those contents at draw time, not at VertexAttribPointer time.
     * Client arrays on nonzero VAOs are invalid and must reach the driver. */
    bool client_ok = g_client_arrays && g_default_vao && pointer;
    if (index >= g_attributes || !g_buffers[0].known || (!g_buffers[0].value && !client_ok) ||
        size < 1 || size > 4 || !type_ok || normalized > 1 || stride < 0 || stride > 255) {
        ++g_attrib_counts.bypass;
        if (index < MAX_ATTRIBUTES) g_attribs[index].known = false;
        return forward(GL_STATE_ATTRIB_POINTER, false);
    }
    struct attribute *a = &g_attribs[index];
    bool same = a->known && a->buffer == g_buffers[0].value && a->size == size &&
                a->type == type && a->normalized == normalized &&
                a->stride == stride && a->pointer == pointer;
    if (!a->known) ++g_attrib_counts.first;
    else if (!same) ++g_attrib_counts.changed;
    *a = (struct attribute){g_buffers[0].value, type, normalized, size, stride, pointer, true};
    return forward(GL_STATE_ATTRIB_POINTER, same);
}

void gl_state_textures_created(int32_t count, const uint32_t *names) {
    if (!names) return;
    for (int32_t i = 0; i < count; ++i) {
        uint32_t name = names[i];
        if (name) g_texture_names[name % TEXTURE_SLOTS] = (struct texture_identity){name, 0};
    }
}

void gl_state_textures_deleted(int32_t count, const uint32_t *names) {
    if (names) {
        for (int32_t i = 0; i < count; ++i) {
            struct texture_identity *id = &g_texture_names[names[i] % TEXTURE_SLOTS];
            if (id->name == names[i]) memset(id, 0, sizeof(*id));
        }
    }
    gl_state_invalidate();
}

void gl_state_program_status(uint32_t program, uint32_t pname, const int32_t *value) {
    if (program && pname == LINK_STATUS && value) {
        g_program_names[program % PROGRAM_SLOTS] = (struct program_identity){program, *value == 1};
        if (!*value) g_program.known = false;
    }
}

void gl_state_program_changed(uint32_t program) {
    struct program_identity *id = &g_program_names[program % PROGRAM_SLOTS];
    if (id->name == program) memset(id, 0, sizeof(*id));
    g_program.known = false;
}

void gl_state_report(double seconds) {
    static const char *const names[] = {
        "glBindBuffer", "glBindTexture", "glUseProgram", "glActiveTexture", "glVertexAttribPointer"
    };
    if (seconds <= 0) seconds = 1;
    for (int i = 0; i < GL_STATE_OPERATION_COUNT; ++i) {
        const struct operation_count *c = &g_counts[i];
        double skipped = c->requested ? 100.0 * (c->requested - c->forwarded) / c->requested : 0;
        log_info("perf: GL %s guest/s %.0f driver/s %.0f skipped %.1f%%", names[i],
                 c->requested / seconds, c->forwarded / seconds, skipped);
    }
    log_info("perf: GL attrib client/s %.0f vbo/s %.0f unknown/s %.0f changed/s %.0f first/s %.0f bypass/s %.0f client_ptr=0x%lx",
             g_attrib_counts.client / seconds, g_attrib_counts.vbo / seconds,
             g_attrib_counts.unknown / seconds, g_attrib_counts.changed / seconds,
             g_attrib_counts.first / seconds, g_attrib_counts.bypass / seconds,
             (unsigned long)g_attrib_counts.client_pointer);
    gl_state_reset_counts();
}
