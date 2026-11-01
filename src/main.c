#include "bindings.h"
#include "host.h"
#include "install.h"
#include "launcher.h"
#include "loader.h"
#include "reimagined.h"

#include <SDL.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(path) _mkdir(path)
#else
#include <unistd.h>
#define make_dir(path) mkdir(path, 0777)
#endif

char g_data_root[1024];
char g_root[1024];
char g_user_dir[1024];
char g_obb_path[1024];
int g_log_verbose;

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [--data DIR] [--play] [--verbose]\n"
            "  --data DIR   launcher folder (default: game/ next to the executable)\n"
            "  --play       skip the launcher and start the last mode played\n"
            "  --install PKG  install a mode from an APK or an iOS IPA and exit\n"
            "  --obb FILE     remember the OBB archive and exit\n",
            argv0);
}

static void fatal(const char *message) {
    log_error("%s", message);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Black Ops Zombies", message, NULL);
    exit(1);
}

static void register_all(void) {
    register_runtime_imports();
    register_config_imports();
    register_timer_imports();
    register_memory_imports();
    register_file_imports();
    register_gl_imports();
    register_egl_imports();
    register_input_imports();
    register_audio_imports();
    register_socket_imports();
}

/* boz-reimagined's readname (0x410d00) reads this file for the profile and co-op name. */
static void write_reimagined_name(void) {
    char path[1300];
    snprintf(path, sizeof(path), "%s/save/data-gles1", g_user_dir);
    make_dir(path);
    snprintf(path, sizeof(path), "%s/save/data-gles1/boz_name.txt", g_user_dir);
    FILE *f = fopen(path, "w");
    if (f) {
        for (const char *p = host_player_name(); *p; ++p) {
            fputc(toupper((unsigned char)*p), f);
        }
        fclose(f);
    }
}

/*
 * The original build names co-op players snprintf("Player-%d", rand()). As in the
 * handheld port, point that reference at the configured name instead. Only the
 * displayed name changes, so co-op with phones still works.
 */
static void override_original_name(uint32_t base) {
    static const struct {
        uint32_t ref, ref_pc, format;
    } builds[] = {
        {0x18f74c, 0x18f60a, 0x3af131}, /* Android */
        {0x09f080, 0x09ef3e, 0x39418c}, /* iOS 1.3.5 */
    };
    uint8_t *ref = NULL;
    uint32_t rel, ref_pc = 0;
    for (size_t i = 0; i < sizeof(builds) / sizeof(builds[0]) && !ref; ++i) {
        uint8_t *candidate = G2H(base + builds[i].ref);
        memcpy(&rel, candidate, 4);
        if (rel == builds[i].format - builds[i].ref_pc &&
            memcmp(G2H(base + builds[i].format), "Player-%d", 10) == 0) {
            ref = candidate;
            ref_pc = builds[i].ref_pc;
        }
    }
    if (!ref) {
        return;
    }
    char name[16];
    size_t n = 0;
    for (const char *p = host_player_name(); *p && n < 13; ++p) {
        if (*p != '%') {
            name[n++] = *p;
        }
    }
    name[n] = 0;
    rel = arena_strdup(name) - (base + ref_pc);
    memcpy(ref, &rel, 4);
}

/*
 * Reload on physical buttons (from boz-reimagined's patch_s3e.py). The original build
 * reloads only through its touch button's state, which the Xperia profile never sets,
 * so the reload key did nothing. These four edits only change how the local button is
 * read; nothing that goes over the network.
 */
/*
 * The game caps itself at 30 fps: once per frame a function spins on s3eTimerGetMs/
 * s3eDeviceYield until 33/33/34 ms have passed (a 3-frame, 100 ms cycle). Gameplay uses the
 * measured frame time, so with the cap gone the frame rate is set by our own limiter / vsync.
 * Found by its byte pattern (same function in the original and the reimagined build).
 */
static void patch_frame_cap(const struct s3e_image *image) {
    if (g_opt.fps_limit == 30) {
        return; /* "30 (original)" keeps the game's own pacing */
    }
    /* cmp r3,#2; ite eq; movs r3,#34; movs r3,#33 -- the 33/33/34 ms cycle; then walk back to
     * the function's push {r0-r4,lr} (the original and reimagined builds differ around it) */
    static const uint8_t sig[] = {0x02, 0x2b, 0x0c, 0xbf, 0x22, 0x23, 0x21, 0x23};
    uint8_t *mem = G2H(image->base);
    uint32_t size = image->header.code_mem_size;
    for (uint32_t i = 64; i + sizeof(sig) < size; i += 2) {
        if (memcmp(mem + i, sig, sizeof(sig)) != 0) {
            continue;
        }
        for (uint32_t back = 2; back <= 64; back += 2) {
            uint8_t *fn = mem + i - back;
            if (fn[1] == 0xb5 && (fn[0] & 0x10)) { /* push {r0-r4, lr} */
                /* keep its s3eDeviceYield(0) (the game's original per-frame yield, which delivers input
                 * on Android): push; adr.w r4; movs r0,#0; blx yield | +12: pop {r0-r4,pc}; nop */
                uint8_t *at = fn + 12;
                if ((at[1] & 0xf8) != 0xf0 || (at[3] & 0xd0) != 0xc0) {
                    break;
                }
                static const uint8_t ret[] = {0x1f, 0xbd, 0x00, 0xbf};
                memcpy(at, ret, sizeof(ret));
                log_info("frame cap: game 30 fps limiter disabled at 0x%08x, yield kept (limit: %s)",
                         image->base + i - back, g_opt.fps_limit > 0 ? "launcher setting" : "none");
                return;
            }
        }
    }
    log_info("frame cap: limiter not found, the game keeps 30 fps");
}

static void patch_original_reload(void) {
    static const struct {
        uint32_t off;
        uint8_t old[4], new_[4];
        int n;
    } patches[] = {
        {0x1DBFC6, {0x40, 0xf3, 0x8d, 0x80}, {0x00, 0xf0, 0x8d, 0xb8}, 4}, /* quick tap reloads */
        {0x1DC0E8, {0x05, 0xdc}, {0x05, 0xe0}, 2},                         /* no touch-state check */
        {0x1DC116, {0x84, 0xf8, 0x51, 0x50}, {0xaf, 0xf3, 0x00, 0x80}, 4}, /* stays armed */
        {0x1DC0FA, {0x73, 0xb1}, {0x00, 0xbf}, 2},                         /* first reload too */
    };
    int applied = 0;
    for (size_t i = 0; i < sizeof(patches) / sizeof(patches[0]); ++i) {
        uint8_t *at = G2H(0x4a000000u + patches[i].off - 0x35090u);
        if (memcmp(at, patches[i].old, (size_t)patches[i].n) == 0) {
            memcpy(at, patches[i].new_, (size_t)patches[i].n);
            applied++;
        }
    }
    log_info("original build: reload fix %d/4", applied);
}

int main(int argc, char **argv) {
    const char *data = NULL, *install = NULL, *obb = NULL;
    bool play = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            data = argv[++i];
        } else if (strcmp(argv[i], "--install") == 0 && i + 1 < argc) {
            install = argv[++i];
        } else if (strcmp(argv[i], "--obb") == 0 && i + 1 < argc) {
            obb = argv[++i];
        } else if (strcmp(argv[i], "--play") == 0) {
            play = true;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            g_log_verbose = 1;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (getenv("CODBOZ_VERBOSE")) {
        g_log_verbose = 1;
    }
    if (data) {
        snprintf(g_data_root, sizeof(g_data_root), "%s", data);
    } else {
        char *base = SDL_GetBasePath();
        snprintf(g_data_root, sizeof(g_data_root), "%sgame", base ? base : "./");
        SDL_free(base);
    }
    make_dir(g_data_root);
    if (install || obb) {
        int ok = 1;
        if (install) {
            enum game_mode installed;
            char error[256];
            ok = install_package(install, g_data_root, &installed, error, sizeof(error));
            if (ok) {
                printf("%s installed\n", mode_dir_name(installed));
            } else {
                printf("%s\n", error);
            }
            if (ok) {
                launcher_remember(installed, NULL);
            }
        }
        if (obb && ok) {
            launcher_remember(-1, obb);
        }
        return ok ? 0 : 1;
    }
    settings_load();
    char path[1300];
    snprintf(path, sizeof(path), "%s/bindings.txt", g_data_root);
    bindings_load(path);

    /* matches codboz-pc.desktop so Wayland/X11 docks show our icon */
    SDL_SetHint("SDL_VIDEO_WAYLAND_WMCLASS", "codboz-pc");
    SDL_SetHint("SDL_VIDEO_X11_WMCLASS", "codboz-pc");
    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (!emu_init()) {
        /* Initialization errors are already logged. In particular, a rejected
         * experimental TLB setting must exit instead of blocking on a dialog. */
        log_error("Unable to initialize the ARM emulator; exiting.");
        emu_shutdown();
        SDL_Quit();
        return 1;
    }
    if (!video_init()) {
        fatal("Unable to open an OpenGL window.");
    }

    enum game_mode mode;
    if (!play || !launcher_saved_choice(&mode, g_obb_path, sizeof(g_obb_path))) {
        mode = launcher_run(g_obb_path, sizeof(g_obb_path));
    }
    const char *mode_dir = mode_dir_name(mode);
    if (mode == MODE_IOS) {
        extern void zeroconf_set_ios_layout(int enabled);
        zeroconf_set_ios_layout(1);
        extern void input_set_native_pause_mode(bool enabled);
        input_set_native_pause_mode(true);
        g_obb_path[0] = 0; /* the iOS data archive sits in its own assets folder */
    }
    snprintf(g_root, sizeof(g_root), "%s/%s", g_data_root, mode_dir);
    snprintf(g_user_dir, sizeof(g_user_dir), "%s/user", g_root);
    make_dir(g_user_dir);
    snprintf(path, sizeof(path), "%s/save", g_user_dir);
    make_dir(path);
    log_info("mode %s, OBB %s", mode_dir, g_obb_path);

    struct s3e_image image;
    snprintf(path, sizeof(path), "%s/assets/boz.s3e.unpacked", g_root);
    if (!s3e_image_load(path, &image)) {
        fatal("Game data not found. Install it from the launcher with your own APK.");
    }
    register_all();
    config_load_image(image.file_data + image.header.config_offset, image.header.config_size);

    video_enter_game();
    /* the menu recognition data and TV art: from this mode, else from the reimagined install */
    snprintf(path, sizeof(path), "%s/assets/boz_menus.json", g_root);
    FILE *probe = fopen(path, "rb");
    if (probe) {
        fclose(probe);
        snprintf(path, sizeof(path), "%s/assets", g_root);
    } else {
        snprintf(path, sizeof(path), "%s/reimagined/assets", g_data_root);
    }
    reimagined_init(path);
    if (mode == MODE_REIMAGINED) {
        extern void menus_init(const char *assets_dir);
        menus_init(path);
    }
    audio_init();
    input_init();

    if (!s3e_image_map(&image, import_resolve)) {
        fatal("Unable to load the game executable.");
    }
    if (mode == MODE_REIMAGINED) {
        write_reimagined_name();
        reimagined_patch_waves();
    } else {
        override_original_name(image.base);
        patch_original_reload();
    }
    patch_frame_cap(&image);
    log_info("boz.s3e mapped at 0x%08x, entry 0x%08x, %zu imports", image.base, image.entry,
             image.symbol_count);

    int rc = emu_run(image.entry);
    log_info("game returned %d", rc);
    audio_shutdown();
    video_shutdown();
    emu_shutdown();
    SDL_Quit();
    return rc;
}
