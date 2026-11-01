#ifndef CODBOZ_PC_EMU_H
#define CODBOZ_PC_EMU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Guest memory is mapped at the same numeric address in the host process and in
 * the JIT, so a 32-bit guest pointer can be dereferenced directly by host code.
 */
enum {
    GUEST_STACK_BASE = 0x40000000u,
    GUEST_STACK_SIZE = 0x01000000u,
    GUEST_ARENA_BASE = 0x41000000u, /* host-owned objects the guest can see */
    GUEST_ARENA_SIZE = 0x04000000u,
    GUEST_THUNK_BASE = 0x45000000u,
    GUEST_THUNK_SIZE = 0x00010000u,
    GUEST_HEAP_BASE = 0x50000000u,
#ifdef _WIN32
    /* Windows always maps KUSER_SHARED_DATA at 0x7ffe0000 (and more just below it): stop short.
     * A Call of the Dead match peaks around 155 MB of guest heap. */
    GUEST_HEAP_SIZE = 0x2fc00000u,
#else
    GUEST_HEAP_SIZE = 0x40000000u,
#endif
};

/* Address the guest returns to when a host-initiated call finishes. */
#define GUEST_RETURN_MAGIC (GUEST_THUNK_BASE + GUEST_THUNK_SIZE - 8u)

typedef void (*import_fn)(void);

/* Frame-time breakdown, logged once a second with CODBOZ_PERF=1 (see overlay.c). */
struct perf_counters {
    uint64_t calls;    /* guest -> host calls (GL, timers, ...) */
    uint64_t host_us;  /* wall time inside host calls, including waits */
    uint64_t wait_us;  /* of which: sleeping because the game asked to yield */
    uint64_t swap_us;  /* of which: SDL_GL_SwapWindow (vsync) + frame pacing */
};
extern struct perf_counters g_perf;

static inline void *G2H(uint32_t addr) {
    return (void *)(uintptr_t)addr;
}

static inline uint32_t H2G(const void *ptr) {
    return (uint32_t)(uintptr_t)ptr;
}

bool emu_init(void);
void emu_shutdown(void);
bool emu_map_identity(uint32_t addr, uint32_t size);
uint32_t emu_add_thunk(const char *name, import_fn fn);
const char *emu_thunk_name(uint32_t index);

/* Arguments of the import currently being serviced (AAPCS softfp). */
uint32_t arg(int index);
uint64_t arg64(int index);
float argf(int index);
void ret(uint32_t value);
void ret64(uint64_t value);
void retf(float value);
const char *current_import(void);

/* Call a guest function (Thumb if bit 0 is set) and return r0. */
uint32_t guest_call(uint32_t fn, int argc, const uint32_t *argv);
uint32_t guest_call0(uint32_t fn);
uint32_t guest_call2(uint32_t fn, uint32_t a, uint32_t b);
uint32_t guest_call3(uint32_t fn, uint32_t a, uint32_t b, uint32_t c);

/* Run the image entry point; returns when the game exits. */
int emu_run(uint32_t entry);
void emu_dump_state(const char *why);

/* Guest heap (identity mapped, visible to the guest). */
void gheap_init(void);
uint32_t gmalloc(uint32_t size);
uint32_t gcalloc(uint32_t count, uint32_t size);
uint32_t grealloc(uint32_t ptr, uint32_t size);
void gfree(uint32_t ptr);
uint32_t gsize(uint32_t ptr);

/* Permanent host-owned allocations inside guest space (strings, tables). */
void *arena_alloc(size_t size);
uint32_t arena_strdup(const char *text);

#endif
