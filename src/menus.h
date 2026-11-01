#ifndef CODBOZ_PC_MENUS_H
#define CODBOZ_PC_MENUS_H

#include <stdbool.h>

/*
 * boz-reimagined's redrawn menus (BrandScreen, MainMenu, StyledScreen, CharacterMenu
 * and the focus/curtain parts of Cursor.java). Coordinates are 1280x720.
 */
void menus_init(const char *assets_dir);
bool menus_enabled(void);
void menus_frame(void); /* logic, once per frame */
void menus_draw(void);
/* Pointer and focus input while the mouse is a cursor; true = consumed. */
void menus_hover(float x, float y);
bool menus_click(float x, float y);
bool menus_pointer_passthrough(float x, float y);
bool menus_nav(int dir); /* 0 left, 1 right, 2 up, 3 down */
bool menus_activate(void);
bool menus_back(void);
/* The name editor takes the keyboard while open. */
bool menus_editing_name(void);
void menus_name_text(const char *text);
void menus_name_key(int scancode);
/* Focus brackets follow the keyboard/controller, not the mouse. */
void menus_set_pad_mode(bool pad);

#endif
