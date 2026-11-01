#ifndef CODBOZ_PC_BINDINGS_H
#define CODBOZ_PC_BINDINGS_H

#include <stdbool.h>

enum bind_action {
    BIND_FORWARD,
    BIND_BACK,
    BIND_LEFT,
    BIND_RIGHT,
    BIND_SHOOT,
    BIND_AIM,
    BIND_RELOAD,
    BIND_USE,
    BIND_MELEE,
    BIND_GRENADE,
    BIND_TACTICAL,
    BIND_CLAYMORE,
    BIND_CROUCH,
    BIND_SWITCH,
    BIND_PAUSE,
    BIND_NUKE, /* Dead Ops Arcade special weapon */
    BIND_BOOST, /* Dead Ops Arcade speed boost (lightning icon) */
    BIND_COUNT,
};

enum { BIND_SLOTS = 4 };

/* An input is a short text: "KEY:W", "MOUSE:1", "WHEEL:UP", "PAD:a", "PADAXIS:righttrigger". */
void bindings_reset(void);
bool bindings_load(const char *path);
bool bindings_save(const char *path);
const char *bindings_id(enum bind_action action);
const char *bindings_label(enum bind_action action);
const char *bindings_get(enum bind_action action, int slot);
void bindings_set(enum bind_action action, int slot, const char *input);
/* Human-readable name of an input ("W", "Mouse 1", "Pad A"). */
void bindings_describe(const char *input, char *out, int size);

/* Live state, polled once per pump. keyboard_mouse = mouse is captured. */
void bindings_wheel(int y);
bool bindings_down(enum bind_action action, bool keyboard_mouse, void *controller);
void bindings_end_frame(void);

#endif
