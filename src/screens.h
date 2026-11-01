#ifndef CODBOZ_PC_SCREENS_H
#define CODBOZ_PC_SCREENS_H

#include <stdbool.h>
#include <stdint.h>

struct screen_item {
    float x, y, w, h; /* 1280x720 */
    float tx, ty;     /* where a press touches the Flash menu */
    char to[24];      /* screen it opens, "*" = unknown, "" = not said */
    bool slider, stay, to_game;
};

bool screens_load(const char *json_path);
/* The focusable items measured on a screen (menus.py), or NULL. */
const struct screen_item *screens_items(const char *id, int *count);
void screens_art_signature(int out[48]);
bool screens_available(void);
/* Called once per frame before anything is drawn over the game. */
void screens_capture(uint32_t fbo, int width, int height);
/* Recognised menu id ("main", "pause", ...) or NULL while in game / loading. */
const char *screens_current(void);
bool screens_scene_stable(void);
const char *screens_last_known(void);
uint64_t screens_changed_ms(void);
bool screens_is(const char *id);
bool screens_coop_session(void);

#endif
