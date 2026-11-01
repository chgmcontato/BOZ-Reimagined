#ifndef CODBOZ_PC_REIMAGINED_H
#define CODBOZ_PC_REIMAGINED_H

#include <stdbool.h>

/* Loads boz_menus.json, the comic font and the HUD art from the installed assets. */
void reimagined_init(const char *assets_dir);
bool reimagined_enabled(void);
/* The running binary carries the boz-reimagined match timer. */
bool reimagined_patched(void);
bool reimagined_match_active(void);
const char *reimagined_map(void);
bool reimagined_deadops(void);
void reimagined_patch_waves(void); /* before the game runs */
int reimagined_zombie_count(int round, int players);
bool reimagined_coop_match(void); /* 2+ players connected in the current match */ /* Dead Ops Arcade level loaded (original and reimagined) */ /* native level name; "deadops" in Dead Ops Arcade */
void reimagined_toggle_timer(void);
void reimagined_frame(void);
void reimagined_draw(void);

#endif
