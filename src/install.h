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

/* SHA-256 of assets/boz.s3e in the Android 1.0.11 APK (versionCode 1045111), the build that
 * boz-redux-gamedef describes (its image_sha256). Code that relies on that build's addresses
 * checks install_image_reference() first. */
#define INSTALL_ORIGINAL_S3E_SHA256 "f458c15a7111779ad320af377d0bb751294119788ba06d430bee0cc977539fee"

/* 1: the mode's boz.s3e is that reference build; 0: another build; -1: unknown (not installed,
 * or a mode without a reference: Reimagined's is patched, iOS is another executable). The hash
 * is recorded at install (boz.s3e.sha256 next to it) or computed once for older installs. */
int install_image_reference(const char *root, enum game_mode mode);

/* Installs ROOT/ios/assets from an iOS IPA: decodes the game image embedded in the armv7
 * executable, and extracts blackops_gles1.dz and the music. */
int install_ipa(const char *ipa_path, const char *root, char *error, size_t error_size);

/* APK or IPA, by content. */
int install_package(const char *path, const char *root, enum game_mode *mode_out, char *error,
                    size_t error_size);

#endif
