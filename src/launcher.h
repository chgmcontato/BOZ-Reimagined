#ifndef CODBOZ_PC_LAUNCHER_H
#define CODBOZ_PC_LAUNCHER_H

#include "install.h"

#include <stdbool.h>
#include <stddef.h>

/* Shows the launcher in the (already open) window until the player presses PLAY. */
enum game_mode launcher_run(char *obb, size_t obb_size);
/* The last choice, when it is still playable (for --play). */
bool launcher_saved_choice(enum game_mode *mode, char *obb, size_t obb_size);

/* Saves the mode (if >= 0) and/or the OBB path in launcher.cfg. */
void launcher_remember(int mode, const char *obb);

#endif
