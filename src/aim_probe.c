/*
 * Read-only probe for direct mouse aim (CODBOZ_AIM_PROBE=1). It never writes guest memory.
 * Logs what the Android 1.0.11 CPlayerController holds, next to the mouse motion, to check the
 * static analysis against a real match:
 *   +0x170 yaw (radians, wraps at 2 pi)    +0x174 pitch (clamped to MaxPitch)
 *   +0x148, +0x14c stick deflection        +0x144 touch factor    +0x154 touch active
 *   [+0x88]+0xfa  turret/vehicle: the turn takes another path
 */
#include "host.h"

#include <stdlib.h>

enum {
    VTABLE = 0x4a400798,
    TYPEINFO = 0x4a40087c,
    SCAN_BYTES = 256u << 20,
    MAX_FOUND = 4,
};

static uint32_t rd32(uint32_t addr) {
    return *(volatile uint32_t *)G2H(addr);
}

static float rdf(uint32_t addr) {
    union {
        uint32_t u;
        float f;
    } v = {rd32(addr)};
    return v.f;
}

void aim_probe_frame(bool in_match, int mouse_dx, int mouse_dy) {
    static int enabled = -1;
    static uint32_t found[MAX_FOUND];
    static int found_count;
    static uint64_t next_scan, next_log;
    static long sum_dx, sum_dy;
    if (enabled < 0) {
        enabled = getenv("CODBOZ_AIM_PROBE") != NULL;
        if (enabled && rd32(VTABLE - 4) != TYPEINFO) {
            log_info("aim probe: unsupported game build (CPlayerController not at the known address)");
            enabled = 0;
        }
    }
    if (!enabled) {
        return;
    }
    if (!in_match) {
        found_count = 0;
        next_scan = 0;
        sum_dx = sum_dy = 0;
        return;
    }
    sum_dx += mouse_dx;
    sum_dy += mouse_dy;
    uint64_t now = monotonic_ms();
    for (int i = 0; i < found_count; ++i) {
        if (rd32(found[i]) != VTABLE) { /* freed: look again */
            found_count = 0;
            next_scan = 0;
        }
    }
    if (!found_count && now >= next_scan) {
        next_scan = now + 3000;
        uint32_t size = (uint32_t)GUEST_HEAP_SIZE < SCAN_BYTES ? (uint32_t)GUEST_HEAP_SIZE : SCAN_BYTES;
        const volatile uint32_t *heap = G2H((uint32_t)GUEST_HEAP_BASE);
        for (uint32_t i = 0; i < size / 4 && found_count < MAX_FOUND; ++i) {
            if (heap[i] == VTABLE) {
                found[found_count++] = (uint32_t)GUEST_HEAP_BASE + i * 4;
            }
        }
        log_info("aim probe: %d controller(s), scan took %llu ms", found_count,
                 (unsigned long long)(monotonic_ms() - now));
    }
    if (found_count && now >= next_log) {
        next_log = now + 250;
        for (int i = 0; i < found_count; ++i) {
            uint32_t c = found[i], link = rd32(c + 0x88);
            bool linked = link >= (uint32_t)GUEST_HEAP_BASE &&
                          link < (uint32_t)GUEST_HEAP_BASE + (uint32_t)GUEST_HEAP_SIZE - 0x100 &&
                          (rd32(link + 0xf8) >> 16 & 0xffu) != 0;
            log_info("aim probe: #%d %08x yaw %.4f pitch %.4f stick %.3f %.3f factor %.3f touch %u linked %d "
                     "mouse %ld %ld",
                     i, c, rdf(c + 0x170), rdf(c + 0x174), rdf(c + 0x148), rdf(c + 0x14c), rdf(c + 0x144),
                     rd32(c + 0x154) & 0xffu, linked, sum_dx, sum_dy);
        }
        sum_dx = sum_dy = 0;
    }
}
