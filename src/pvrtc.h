#ifndef CODBOZ_PC_PVRTC_H
#define CODBOZ_PC_PVRTC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* GL_IMG_texture_compression_pvrtc formats */
enum {
    PVRTC_RGB_4BPP = 0x8c00,
    PVRTC_RGB_2BPP = 0x8c01,
    PVRTC_RGBA_4BPP = 0x8c02,
    PVRTC_RGBA_2BPP = 0x8c03,
};

static inline bool pvrtc_is_format(uint32_t format) {
    return format >= PVRTC_RGB_4BPP && format <= PVRTC_RGBA_2BPP;
}

/* Bytes of compressed data for one level (blocks are at least 2x2). */
size_t pvrtc_data_size(int width, int height, int bpp2);

/* Decodes one level into width*height*4 RGBA bytes. Width/height must be powers of two. */
bool pvrtc_decode(const void *src, size_t src_size, int width, int height, int bpp2,
                  uint8_t *rgba);

uint32_t pvrtc_twiddle(uint32_t xs, uint32_t ys, uint32_t x, uint32_t y);

#endif
