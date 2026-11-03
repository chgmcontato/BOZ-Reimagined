/* OpenGL ES builds only (Android): on-screen controls for gameplay. See touch_controls.c. */
#ifndef CODBOZ_TOUCH_CONTROLS_H
#define CODBOZ_TOUCH_CONTROLS_H

#include <stdbool.h>
#include <stdint.h>

enum { TOUCH_DOWN, TOUCH_MOVE, TOUCH_UP };

/* A finger, in the game's 1280x720 reference units (outside that range on the side bars). */
void touch_controls_finger(int64_t id, int phase, float x, float y);
void touch_controls_reset(void);
void touch_controls_move(float *x, float *y);
void touch_controls_take_look(float *dx, float *dy);
bool touch_controls_take_pause(void);
bool touch_controls_take_fps(void);
/* the joystick is pushed to its edge: hold the sprint key */
bool touch_controls_sprint(void);
/* enum bind_action */
bool touch_controls_down(int action);
void touch_controls_draw(void);

#endif
