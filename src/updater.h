#ifndef CODBOZ_PC_UPDATER_H
#define CODBOZ_PC_UPDATER_H

/*
 * Automatic updates from the project's GitHub releases. A release build (one made with
 * `make VERSION=x.y.z`) asks GitHub for the latest release in a background thread; when it
 * is newer, it downloads this platform's archive, checks it against the release's
 * SHA256SUMS.txt and puts the new executable in place of the running one. The launcher
 * then restarts into it. Development builds (no VERSION) never update themselves.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum updater_state {
    UPDATER_OFF,         /* development build, disabled in Settings, or CODBOZ_NO_UPDATE */
    UPDATER_CHECKING,    /* asking GitHub for the latest release */
    UPDATER_CURRENT,     /* this is the latest version (or the check failed: offline) */
    UPDATER_DOWNLOADING, /* a newer release exists: downloading and installing it */
    UPDATER_READY,       /* the new executable is in place; restart to use it */
    UPDATER_FAILED,      /* a newer release exists but could not be installed */
};

/* Remembers how to restart, cleans up after the last update and, when enabled, starts the
 * background check. */
void updater_init(int argc, char **argv, bool enabled);
/* The Settings switch: on starts a check (if none ran or the last one failed); off keeps a
 * download in progress from replacing the executable. */
void updater_set_enabled(bool enabled);
enum updater_state updater_state(void);
/* This build's version ("0.5.2"), or "dev". */
const char *updater_version(void);
/* The release being installed or installed (valid from UPDATER_DOWNLOADING on). */
const char *updater_new_version(void);
/* Why UPDATER_FAILED happened, in English (for the log and Settings). */
const char *updater_error(void);
/* When UPDATER_READY: closes SDL and starts the new executable with the same arguments
 * (exec on Linux, a new process on Windows). Returns only if that could not be done. */
void updater_restart(void);

/* ---- internals, exposed for tools/test_updater.c ---- */

/* <0, 0, >0 like strcmp, for "v1.2.3"-style versions (a leading v is optional). */
int updater_compare_versions(const char *a, const char *b);
void updater_sha256_hex(const void *data, size_t size, char hex[65]);
/* The executable inside a release archive (zip on Windows, tar.gz on Linux), malloc'd. */
uint8_t *updater_extract(const uint8_t *archive, size_t size, bool zip, const char *name,
                         size_t *out_size);
/* The hash listed for `file` in a SHA256SUMS.txt body, or false. */
bool updater_find_sum(const char *sums, const char *file, char hex[65]);
/* Replaces the file at exe_path with data (keeps <exe>.old on Windows, where the running
 * image cannot be overwritten). */
bool updater_replace_exe(const char *exe_path, const uint8_t *data, size_t size, char *err,
                         size_t err_size);
/* The whole update for `current` → latest release, in this thread. new_version gets the tag
 * of a newer release (empty when current is already the latest). */
bool updater_run(const char *current, const char *exe_path, char *new_version,
                 size_t new_version_size, char *err, size_t err_size);

#endif
