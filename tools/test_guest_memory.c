/*
 * Core emulator check, no game data needed: the real guest memory layout (emu_init), the
 * Dynarmic A32 JIT running ARM and Thumb code, an import reached by SVC with a stack argument,
 * a nested guest call from inside that import (a second Jit), ldrex/strex, VFP, and every host
 * access going through G2H. Run it on each build flavour:
 *
 *   identity map (PC):          -DCODBOZ_GUEST_BASE undefined
 *   4 GiB reservation (Android): -DCODBOZ_GUEST_BASE
 *
 * Build: see mobile/README.md ("Testes sem o aparelho"); it links src/emu_dynarmic.cpp,
 * src/gheap.c and Dynarmic. Exit status 0 = every check passed.
 */
#include "emu.h"
#include "log.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int g_log_verbose;

uint64_t monotonic_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/*
 * Assembled with arm-linux-gnueabihf-as (armv7-a, vfpv3); offsets of the three functions:
 *   0x00 ARM   arm_sum(p, thunk): r = thunk(p[0], p[1], 7, 9, 11 on the stack); p[2] = r;
 *                                 p[3] += 1 with ldrex/strex; return r
 *   0x50 Thumb thumb_vfp(p): float at +24 = float +16 + float +20; halfword +28 copied to +30
 *   0x66 Thumb cb(x) = 3 * x
 */
static const uint8_t k_code[] = {
    0x30, 0x40, 0x2d, 0xe9, 0x04, 0xd0, 0x4d, 0xe2, 0x00, 0x40, 0xa0, 0xe1, 0x01, 0x50, 0xa0, 0xe1,
    0x0b, 0x00, 0xa0, 0xe3, 0x00, 0x00, 0x8d, 0xe5, 0x00, 0x00, 0x94, 0xe5, 0x04, 0x10, 0x94, 0xe5,
    0x07, 0x20, 0xa0, 0xe3, 0x09, 0x30, 0xa0, 0xe3, 0x35, 0xff, 0x2f, 0xe1, 0x08, 0x00, 0x84, 0xe5,
    0x0c, 0x20, 0x84, 0xe2, 0x9f, 0x1f, 0x92, 0xe1, 0x01, 0x10, 0x81, 0xe2, 0x91, 0x3f, 0x82, 0xe1,
    0x00, 0x00, 0x53, 0xe3, 0xfa, 0xff, 0xff, 0x1a, 0x04, 0xd0, 0x8d, 0xe2, 0x30, 0x80, 0xbd, 0xe8,
    0x90, 0xed, 0x04, 0x0a, 0xd0, 0xed, 0x05, 0x0a, 0x30, 0xee, 0x20, 0x1a, 0x80, 0xed, 0x06, 0x1a,
    0x81, 0x8b, 0xc1, 0x83, 0x70, 0x47, 0x03, 0x21, 0x48, 0x43, 0x70, 0x47,
};

static uint32_t g_code;
static int g_failures;
static unsigned g_thunk_calls;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

/* The import: sums its five arguments (the fifth from the guest stack) plus cb(first). */
static void imp_add5(void) {
    ++g_thunk_calls;
    uint32_t a = arg(0), b = arg(1), c = arg(2), d = arg(3), e = arg(4);
    uint32_t nested = guest_call2(g_code + 0x66 + 1, a, 0); /* Thumb: bit 0 set */
    ret(a + b + c + d + e + nested);
}

static void check_translation(void) {
    CHECK(G2H(0) == NULL, "G2H(0) must stay NULL");
    CHECK(H2G(NULL) == 0, "H2G(NULL) must be 0");
    CHECK((uintptr_t)G2H(0x1234) == 0x1234, "small values (GL buffer offsets) pass through");
    uint32_t heap = (uint32_t)GUEST_HEAP_BASE + 0x2000u;
    CHECK(H2G(G2H(heap)) == heap, "H2G(G2H(x)) == x");
    CHECK(H2G(G2H(0x4a000000u)) == 0x4a000000u, "round trip at the image base");
#ifdef CODBOZ_GUEST_BASE
    CHECK((uintptr_t)G2H(heap) != heap, "the Android layout must not be an identity map");
    int local = 0;
    CHECK(H2G(&local) == 0, "a host stack pointer is not a guest address");
#else
    CHECK((uintptr_t)G2H(heap) == heap, "the PC layout is an identity map");
#endif
}

int main(void) {
    if (!emu_init()) {
        fprintf(stderr, "FAIL: emu_init\n");
        return 1;
    }
    check_translation();

    /* code in the arena (host-owned, guest-visible), data in the guest heap */
    uint8_t *code = arena_alloc(sizeof(k_code));
    memcpy(code, k_code, sizeof(k_code));
    g_code = H2G(code);
    CHECK(g_code >= GUEST_ARENA_BASE && g_code < GUEST_ARENA_BASE + GUEST_ARENA_SIZE,
          "arena address 0x%08x", g_code);
    uint32_t thunk = emu_add_thunk("add5", imp_add5);

    uint32_t p = gcalloc(1, 64);
    CHECK(p >= GUEST_HEAP_BASE, "gcalloc returned 0x%08x", p);
    uint32_t *w = G2H(p);
    w[0] = 5;
    w[1] = 6;
    w[3] = 41;
    float f[2] = {1.25f, 2.5f};
    memcpy(&w[4], f, sizeof(f));
    ((uint16_t *)G2H(p))[14] = 0xbeef;

    uint32_t r = guest_call2(g_code, p, thunk); /* ARM: bit 0 clear */
    /* 5 + 6 + 7 + 9 + 11 + cb(5) = 38 + 15 */
    CHECK(r == 53, "arm_sum returned %u, expected 53", r);
    CHECK(w[2] == 53, "guest store p[2] = %u", w[2]);
    CHECK(w[3] == 42, "ldrex/strex increment: %u", w[3]);
    CHECK(g_thunk_calls == 1, "import called %u times", g_thunk_calls);

    guest_call(g_code + 0x50 + 1, 1, &p);
    float sum;
    memcpy(&sum, &w[6], sizeof(sum));
    CHECK(sum == 3.75f, "vadd.f32 gave %f", (double)sum);
    CHECK(((uint16_t *)G2H(p))[15] == 0xbeef, "halfword copy");

    /* many calls: the translated blocks are reused and nothing drifts */
    uint64_t t0 = monotonic_us();
    for (int i = 0; i < 20000; ++i) {
        r = guest_call2(g_code, p, thunk);
    }
    uint64_t dt = monotonic_us() - t0;
    CHECK(r == 53 && w[3] == 42 + 20000, "after the loop: r=%u p[3]=%u", r, w[3]);

    uint32_t big = gmalloc(32u << 20);
    CHECK(big >= GUEST_HEAP_BASE, "32 MiB guest allocation");
    if (big) {
        memset(G2H(big), 0x5a, 32u << 20);
        CHECK(((uint8_t *)G2H(big))[(32u << 20) - 1] == 0x5a, "large allocation is writable");
        gfree(big);
    }

#ifdef CODBOZ_GUEST_BASE
    const char *layout = "guest base (Android)";
#else
    const char *layout = "identity (PC)";
#endif
    printf("%s: %s, 20000 guest calls in %" PRIu64 " ms\n", g_failures ? "FAILED" : "OK", layout,
           dt / 1000);
    emu_shutdown();
    return g_failures ? 1 : 0;
}
