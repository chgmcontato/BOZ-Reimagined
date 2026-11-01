#ifndef CODBOZ_PC_UI_H
#define CODBOZ_PC_UI_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 2D drawing over the game (or alone, for the launcher). Coordinates are in the
 * reimagined layer's 1280x720 reference space, letterboxed into the window.
 */
struct ui_image {
    uint32_t tex;
    int w, h;
};

enum ui_align { UI_LEFT, UI_CENTER, UI_RIGHT };

enum { UI_FONT_DISPLAY = 0, UI_FONT_BODY = 1, UI_FONT_COUNT = 2 };

void ui_init(void);
bool ui_load_font(const char *ttf_path); /* into the current slot */
bool ui_load_font_slot(int slot, const char *ttf_path);
void ui_font(int slot);
/* Reference size of the drawing space (default 1280x720, the game's). */
void ui_set_reference(float w, float h);
bool ui_image_load(struct ui_image *image, const char *path);
void ui_image_free(struct ui_image *image);
bool ui_capture_coop_players(struct ui_image *image);

/* Brackets every frame's drawing; saves and restores the game's GL state. */
void ui_begin(int width, int height);
void ui_end(void);

void ui_rect(float x, float y, float w, float h, uint32_t argb);
void ui_frame(float x, float y, float w, float h, float thickness, uint32_t argb);
void ui_image(const struct ui_image *image, float x, float y, float w, float h, float alpha);
/* Text in the comic font with a dark outline; returns the drawn width. */
float ui_text(const char *text, float x, float baseline, float size, uint32_t argb,
              enum ui_align align);
float ui_text_width(const char *text, float size);
/* Same without the outline. */
float ui_label(const char *text, float x, float baseline, float size, uint32_t argb,
               enum ui_align align);
void ui_round_rect(float x, float y, float w, float h, float r, uint32_t argb);
void ui_line(float x0, float y0, float x1, float y1, float width, uint32_t argb);
void ui_triangle(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t argb);
void ui_circle(float cx, float cy, float r, uint32_t argb);
void ui_image_part(const struct ui_image *image, float x, float y, float w, float h, float u0,
                   float v0, float u1, float v1, float alpha);
void ui_image_cover(const struct ui_image *image, float x, float y, float w, float h, float alpha);
/* Four corners, clockwise from top-left. */
void ui_quad(const float pts[8], uint32_t argb);
void ui_quad_outline(const float pts[8], float width, uint32_t argb);
/* Stencil clipping: draw the mask shapes, then mask_use(inside) and the content. */
void ui_mask_begin(void);
void ui_mask_use(bool inside);
void ui_mask_end(void);

/* Window pixels -> reference space (for mouse hit testing). */
void ui_window_to_ref(int wx, int wy, float *rx, float *ry);
float ui_scale(void);

#endif
