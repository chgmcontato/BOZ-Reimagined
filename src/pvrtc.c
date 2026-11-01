/*
 * PVRTC1 (GL_IMG_texture_compression_pvrtc) decoder to RGBA8.
 *
 * The iOS build of the game ships its frontend art as PVRTC; desktop GL has no PVRTC, so
 * glCompressedTexImage2D with these formats is decoded here and uploaded as plain RGBA.
 *
 * Layout (PowerVR "PVRTC texture compression" spec): 64-bit words, one per 4x4 (4bpp) or 8x4
 * (2bpp) pixel block, stored in Morton (twiddled) order. Each word has 32 bits of modulation
 * data and two colours, A and B, defined at the block centre. A pixel's A and B are the bilinear
 * blend of the four nearest block centres (the texture wraps), then mixed by its modulation
 * weight.
 */
#include "pvrtc.h"

#include <stdlib.h>
#include <string.h>

struct rgba5 {
    int r, g, b, a; /* r,g,b in 0..31, a in 0..15 */
};

static struct rgba5 color_a(uint32_t c) {
    struct rgba5 o;
    if (c & 0x8000u) { /* opaque: RGB 554 */
        o.r = (int)((c >> 10) & 0x1f);
        o.g = (int)((c >> 5) & 0x1f);
        o.b = (int)(((c >> 1) & 0xf) << 1 | ((c >> 4) & 1));
        o.a = 0xf;
    } else { /* translucent: ARGB 3443 */
        o.a = (int)(((c >> 12) & 0x7) << 1);
        o.r = (int)(((c >> 8) & 0xf) << 1 | ((c >> 11) & 1));
        o.g = (int)(((c >> 4) & 0xf) << 1 | ((c >> 7) & 1));
        o.b = (int)(((c >> 1) & 0x7) << 2 | ((c >> 2) & 0x3));
    }
    return o;
}

static struct rgba5 color_b(uint32_t c) {
    struct rgba5 o;
    if (c & 0x80000000u) { /* opaque: RGB 555 */
        o.r = (int)((c >> 26) & 0x1f);
        o.g = (int)((c >> 21) & 0x1f);
        o.b = (int)((c >> 16) & 0x1f);
        o.a = 0xf;
    } else { /* translucent: ARGB 3444 */
        o.a = (int)(((c >> 28) & 0x7) << 1);
        o.r = (int)(((c >> 24) & 0xf) << 1 | ((c >> 27) & 1));
        o.g = (int)(((c >> 20) & 0xf) << 1 | ((c >> 23) & 1));
        o.b = (int)(((c >> 16) & 0xf) << 1 | ((c >> 19) & 1));
    }
    return o;
}

/* Morton index of block (x, y) in an xs * ys grid (powers of two); y takes the low bit. */
uint32_t pvrtc_twiddle(uint32_t xs, uint32_t ys, uint32_t x, uint32_t y) {
    uint32_t min_dim = xs < ys ? xs : ys;
    uint32_t out = 0, bit = 1, shift = 0;
    while (bit < min_dim) {
        if (y & bit) out |= 1u << (2 * shift);
        if (x & bit) out |= 2u << (2 * shift);
        bit <<= 1;
        shift++;
    }
    uint32_t rest = (xs < ys ? y : x) >> shift;
    return out | rest << (2 * shift);
}

size_t pvrtc_data_size(int width, int height, int bpp2) {
    int bw = bpp2 ? 8 : 4;
    int xb = width / bw > 2 ? width / bw : 2;
    int yb = height / 4 > 2 ? height / 4 : 2;
    return (size_t)xb * (size_t)yb * 8u;
}

static int is_pow2(int v) {
    return v > 0 && (v & (v - 1)) == 0;
}

/* 2bpp: modulation value (0..3 scale, i.e. weight*8 = {0,3,5,8}) for pixel (x,y) of a block. */
static int mod2_lookup(const uint8_t *words, uint32_t xb, uint32_t yb, int px, int py, int *punch);

bool pvrtc_decode(const void *src, size_t src_size, int width, int height, int bpp2,
                  uint8_t *rgba) {
    if (!is_pow2(width) || !is_pow2(height) || src_size < pvrtc_data_size(width, height, bpp2)) {
        return false;
    }
    const int bw = bpp2 ? 8 : 4, bh = 4;
    const uint32_t xb = (uint32_t)(width / bw > 2 ? width / bw : 2);
    const uint32_t yb = (uint32_t)(height / bh > 2 ? height / bh : 2);
    const uint8_t *words = src;

    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            /* The four block centres around the pixel (centres sit at bw/2, bh/2 in a block). */
            int gx = px - bw / 2, gy = py - bh / 2;
            int x0 = gx >= 0 ? gx / bw : (gx - bw + 1) / bw;
            int y0 = gy >= 0 ? gy / bh : (gy - bh + 1) / bh;
            int fx = gx - x0 * bw, fy = gy - y0 * bh; /* 0..bw-1, 0..bh-1 */
            struct rgba5 a[4], b[4];
            for (int k = 0; k < 4; ++k) {
                uint32_t bx = (uint32_t)((x0 + (k & 1)) + (int)xb) % xb;
                uint32_t by = (uint32_t)((y0 + (k >> 1)) + (int)yb) % yb;
                const uint8_t *w = words + 8u * pvrtc_twiddle(xb, yb, bx, by);
                uint32_t color = (uint32_t)w[4] | (uint32_t)w[5] << 8 | (uint32_t)w[6] << 16 |
                                 (uint32_t)w[7] << 24;
                a[k] = color_a(color);
                b[k] = color_b(color);
            }
            int w00 = (bw - fx) * (bh - fy), w10 = fx * (bh - fy);
            int w01 = (bw - fx) * fy, w11 = fx * fy; /* sum = bw * bh */
#define LERP(arr, ch) (arr[0].ch * w00 + arr[1].ch * w10 + arr[2].ch * w01 + arr[3].ch * w11)
            int area = bw * bh;
            /* Interpolated colours, scaled by area (5-bit colour, 4-bit alpha). */
            int ar = LERP(a, r), ag = LERP(a, g), ab = LERP(a, b), aa = LERP(a, a);
            int br = LERP(b, r), bg = LERP(b, g), bb = LERP(b, b), ba = LERP(b, a);
#undef LERP

            /* Modulation comes from the block holding the pixel. */
            uint32_t hx = (uint32_t)(px / bw), hy = (uint32_t)(py / bh);
            const uint8_t *hw = words + 8u * pvrtc_twiddle(xb, yb, hx, hy);
            uint32_t mod = (uint32_t)hw[0] | (uint32_t)hw[1] << 8 | (uint32_t)hw[2] << 16 |
                           (uint32_t)hw[3] << 24;
            int mode = hw[4] & 1;
            int weight, punch = 0; /* weight of B, in eighths */
            if (!bpp2) {
                int m = (int)((mod >> (2 * ((py % 4) * 4 + (px % 4)))) & 3);
                if (mode) { /* punch-through: 0, 4/8, 4/8 transparent, 1 */
                    static const int pw[4] = {0, 4, 4, 8};
                    weight = pw[m];
                    punch = m == 2;
                } else {
                    static const int sw[4] = {0, 3, 5, 8};
                    weight = sw[m];
                }
            } else {
                weight = mod2_lookup(words, xb, yb, px, py, &punch);
            }

            int r = (ar * (8 - weight) + br * weight); /* scaled by area * 8 */
            int g = (ag * (8 - weight) + bg * weight);
            int bl = (ab * (8 - weight) + bb * weight);
            int al = (aa * (8 - weight) + ba * weight);
            int scale = area * 8;
            uint8_t *o = rgba + ((size_t)py * (size_t)width + (size_t)px) * 4u;
            o[0] = (uint8_t)((r * 255 + scale * 31 / 2) / (scale * 31));
            o[1] = (uint8_t)((g * 255 + scale * 31 / 2) / (scale * 31));
            o[2] = (uint8_t)((bl * 255 + scale * 31 / 2) / (scale * 31));
            o[3] = punch ? 0 : (uint8_t)((al * 255 + scale * 15 / 2) / (scale * 15));
        }
    }
    return true;
}

/*
 * PVRTC 2bpp modulation. Mode bit 0: one bit per pixel (weights 0 / 8). Mode bit 1: only half of
 * the pixels (checkerboard) store 2-bit values; the others are interpolated from neighbours, as
 * selected by the block's flag bits (bit 0 of the modulation word, and bit 20).
 */
static int mod2_value(const uint8_t *words, uint32_t xb, uint32_t yb, int px, int py) {
    /* Stored 2-bit value of a checkerboard pixel, fetched from its own block (wrapping). */
    int w = (int)xb * 8, h = (int)yb * 4;
    px = ((px % w) + w) % w;
    py = ((py % h) + h) % h;
    const uint8_t *bw = words + 8u * pvrtc_twiddle(xb, yb, (uint32_t)(px / 8), (uint32_t)(py / 4));
    uint32_t mod = (uint32_t)bw[0] | (uint32_t)bw[1] << 8 | (uint32_t)bw[2] << 16 |
                   (uint32_t)bw[3] << 24;
    int lx = px % 8, ly = py % 4;
    int mode = bw[4] & 1;
    static const int sw[4] = {0, 3, 5, 8};
    if (!mode) { /* direct 1bpp block: bit per pixel */
        return (mod >> (ly * 8 + lx)) & 1 ? 8 : 0;
    }
    /* Interpolated block: 2-bit values packed for pixels with (x + y) even, 16 of them. */
    int idx = (ly * 8 + lx) >> 1;
    int v = (int)((mod >> (idx * 2)) & 3);
    return sw[v];
}

static int mod2_lookup(const uint8_t *words, uint32_t xb, uint32_t yb, int px, int py, int *punch) {
    *punch = 0;
    const uint8_t *bw = words + 8u * pvrtc_twiddle(xb, yb, (uint32_t)(px / 8), (uint32_t)(py / 4));
    uint32_t mod = (uint32_t)bw[0] | (uint32_t)bw[1] << 8 | (uint32_t)bw[2] << 16 |
                   (uint32_t)bw[3] << 24;
    int mode = bw[4] & 1;
    int lx = px % 8, ly = py % 4;
    if (!mode) {
        return (mod >> (ly * 8 + lx)) & 1 ? 8 : 0;
    }
    if (((lx + ly) & 1) == 0) { /* stored pixel */
        return mod2_value(words, xb, yb, px, py);
    }
    /* Missing pixel: flag bits choose horizontal, vertical or all-four averaging. */
    int flags = (int)(mod & 1) | (int)((mod >> 20) & 1) << 1;
    int l = mod2_value(words, xb, yb, px - 1, py), r = mod2_value(words, xb, yb, px + 1, py);
    int u = mod2_value(words, xb, yb, px, py - 1), d = mod2_value(words, xb, yb, px, py + 1);
    switch (flags) {
    case 1: return (u + d + 1) / 2;   /* vertical */
    case 2: return (l + r + 1) / 2;   /* horizontal */
    default: return (l + r + u + d + 2) / 4;
    }
}
