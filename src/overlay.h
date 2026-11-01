#ifndef CODBOZ_PC_OVERLAY_H
#define CODBOZ_PC_OVERLAY_H

/* Host-drawn layer on top of the game: FPS counter and short hints. */
void overlay_init(void);
void overlay_draw(int width, int height);
void overlay_set_hint(const char *text);

#endif
