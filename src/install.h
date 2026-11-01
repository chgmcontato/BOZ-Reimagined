#ifndef CODBOZ_PC_INSTALL_H
#define CODBOZ_PC_INSTALL_H

#include <stddef.h>

enum game_mode {
    MODE_ORIGINAL = 0,   /* untouched mobile build: plays co-op with the phone version */
    MODE_REIMAGINED = 1, /* boz-reimagined TV build: plays co-op with the TV */
    MODE_IOS = 2,        /* iOS build 1.3.5 from an IPA: its own data, no OBB */
    MODE_COUNT
};

/* Folder of a mode under the data root ("original", "reimagined", "ios"). */
const char *mode_dir_name(enum game_mode mode);

/* Extracts assets/ from the APK into ROOT/<mode>/assets; detects the mode from the APK. */
int install_apk(const char *apk_path, const char *root, enum game_mode *mode_out, char *error,
                size_t error_size);
int install_present(const char *root, enum game_mode mode);

/* Installs ROOT/ios/assets from an iOS IPA: decodes the game image embedded in the armv7
 * executable, and extracts blackops_gles1.dz and the music. */
int install_ipa(const char *ipa_path, const char *root, char *error, size_t error_size);

/* APK or IPA, by content. */
int install_package(const char *path, const char *root, enum game_mode *mode_out, char *error,
                    size_t error_size);

#endif
