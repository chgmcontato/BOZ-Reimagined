#ifndef CODBOZ_PC_INSTALL_H
#define CODBOZ_PC_INSTALL_H

#include <stddef.h>

enum game_mode {
    MODE_ORIGINAL = 0,  /* untouched mobile build: plays co-op with the phone version */
    MODE_REIMAGINED = 1 /* boz-reimagined TV build: plays co-op with the TV */
};

/* Extracts assets/ from the APK into ROOT/<mode>/assets; detects the mode from the APK. */
int install_apk(const char *apk_path, const char *root, enum game_mode *mode_out, char *error,
                size_t error_size);
int install_present(const char *root, enum game_mode mode);

#endif
