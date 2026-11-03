#ifndef CODBOZ_PC_HOST_H
#define CODBOZ_PC_HOST_H

#include "emu.h"
#include "log.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define IMPORT(name) static void imp_##name(void)
#define REG(name) import_register(#name, imp_##name)

void import_register(const char *name, import_fn fn);
uint32_t import_resolve(const char *name);
/* Thunk for a host function exposed through an extension table, not the import list. */
uint32_t import_thunk(const char *name, import_fn fn);

void register_runtime_imports(void);
void register_config_imports(void);
void register_timer_imports(void);
void register_memory_imports(void);
void register_file_imports(void);
void register_gl_imports(void);
void register_egl_imports(void);
void register_input_imports(void);
void register_audio_imports(void);
void register_socket_imports(void);

/* Paths: g_root holds assets/, g_user_dir holds saves and settings. */
extern char g_data_root[1024]; /* launcher folder: settings, bindings, both modes */
extern char g_root[1024];      /* the running mode's folder (holds assets/) */
extern char g_user_dir[1024];  /* the running mode's saves */
extern char g_obb_path[1024];
/* Game data is the gles1 build (the iOS archive): its art is PVRTC, decoded in software. */
extern bool g_pvrtc_textures;

struct host_options {
    int width;
    int height;
    bool fullscreen;
    bool vsync;
    int fps_limit;
    int low_fps_paused; /* FPS while a co-op match waits for a player; 0 = keep fps_limit */
    bool show_fps;
    float mouse_sensitivity;
    float mouse_y_scale; /* stick look: vertical gain relative to horizontal */
    bool invert_y;
    bool mouse_drag; /* mouse = a finger dragging on the screen (1:1), else a look stick */
    bool english;    /* launcher language: English, else Portuguese */
    bool auto_update; /* release builds: install new GitHub releases (updater.c) */
};
extern struct host_options g_opt;

/* config.c */
void config_load_image(const uint8_t *data, uint32_t size);
const char *config_get(const char *section, const char *key);
void config_set(const char *section, const char *key, const char *value);
const char *host_player_name(void);
void settings_load(void);
void settings_save(void);
void settings_set_player_name(const char *name);

/* timer.c */
uint64_t monotonic_us(void);
uint64_t monotonic_ms(void);
void sleep_ms(uint32_t ms);
void dispatch_due_timers(void);
void timer_frame_tick(void);
void timer_perf_reset(void);
void timer_perf_report(double seconds);

/* runtime.c */
int device_quit_requested(void);
void device_request_quit(void);
void dispatch_device_event(uint32_t id, uint32_t system_data);

/* video (egl.c) */
bool video_init(void);
void video_enter_game(void);
void video_shutdown(void);
void video_swap(void);
int video_width(void);
int video_height(void);
void *gl_proc(const char *name);

/* input.c */
bool input_init(void);
void input_pump(void);
void input_after_swap(void);

/* audio.c */
bool audio_init(void);
void audio_pump(void);
void audio_shutdown(void);

/* socket.c */
void socket_pump(void);

/* Pump everything that delivers callbacks into the game. */
void host_pump(void);

#endif
