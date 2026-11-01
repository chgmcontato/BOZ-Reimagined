/* PVRTC decoder checks: cc -O2 -Isrc tools/test_pvrtc.c src/pvrtc.c -o /tmp/t && /tmp/t */
#include "pvrtc.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void put_word(uint8_t *w, uint32_t mod, uint32_t color) {
    for (int i = 0; i < 4; ++i) { w[i] = (uint8_t)(mod >> (8 * i)); w[4 + i] = (uint8_t)(color >> (8 * i)); }
}

int main(void) {
    /* Morton order: square grids interleave y (low bit) and x. */
    CHECK(pvrtc_twiddle(4, 4, 0, 0) == 0, "t00");
    CHECK(pvrtc_twiddle(4, 4, 0, 1) == 1, "y1 -> 1");
    CHECK(pvrtc_twiddle(4, 4, 1, 0) == 2, "x1 -> 2");
    CHECK(pvrtc_twiddle(4, 4, 3, 3) == 15, "t33");
    /* Non-square: the extra high bits of the longer axis are appended. */
    CHECK(pvrtc_twiddle(8, 2, 5, 1) == ((5 >> 1) << 2 | 2 | 1), "rect %u", pvrtc_twiddle(8, 2, 5, 1));
    CHECK(pvrtc_data_size(4, 4, 0) == 32 && pvrtc_data_size(16, 16, 0) == 128, "size");

    /* 8x8 4bpp: every block opaque A = black, B = white. Modulation 0 -> A, 3 -> B. */
    uint8_t data[32], out[8 * 8 * 4];
    uint32_t color = 0x8000u | 0x80000000u | (0x1fu << 26) | (0x1fu << 21) | (0x1fu << 16);
    for (int b = 0; b < 4; ++b) put_word(data + 8 * b, 0x00000000u, color);
    CHECK(pvrtc_decode(data, sizeof data, 8, 8, 0, out), "decode");
    CHECK(out[0] == 0 && out[1] == 0 && out[2] == 0 && out[3] == 255, "mod0 black opaque: %d %d %d %d", out[0], out[1], out[2], out[3]);
    for (int b = 0; b < 4; ++b) put_word(data + 8 * b, 0xffffffffu, color);
    pvrtc_decode(data, sizeof data, 8, 8, 0, out);
    CHECK(out[0] == 255 && out[1] == 255 && out[2] == 255 && out[3] == 255, "mod3 white: %d %d %d %d", out[0], out[1], out[2], out[3]);
    /* Punch-through (mode bit) with value 2 is transparent. */
    for (int b = 0; b < 4; ++b) put_word(data + 8 * b, 0xaaaaaaaau, color | 1u);
    pvrtc_decode(data, sizeof data, 8, 8, 0, out);
    CHECK(out[3] == 0, "punch alpha %d", out[3]);
    /* Uniform colour across blocks stays uniform after bilinear blending (wrap-around). */
    uint32_t red = 0x8000u | (0x1fu << 10) | 0x80000000u | (0x1fu << 26);
    for (int b = 0; b < 4; ++b) put_word(data + 8 * b, 0x55555555u, red);
    pvrtc_decode(data, sizeof data, 8, 8, 0, out);
    int uniform = 1;
    for (int i = 0; i < 64; ++i) uniform &= out[4 * i] == 255 && out[4 * i + 1] == 0 && out[4 * i + 2] == 0;
    CHECK(uniform, "uniform red: %d %d %d", out[0], out[1], out[2]);
    /* Rejects non power-of-two sizes and short buffers. */
    CHECK(!pvrtc_decode(data, sizeof data, 6, 8, 0, out), "npot");
    CHECK(!pvrtc_decode(data, 16, 8, 8, 0, out), "short");
    printf(fails ? "%d failure(s)\n" : "pvrtc: all checks passed\n", fails);
    return fails != 0;
}
