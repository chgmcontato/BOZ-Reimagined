/*
 * Video: an SDL window with a desktop OpenGL compatibility context that stands
 * in for EGL + OpenGL ES 1.1/2.0. ES-only entry points (fixed point, *f
 * variants, ETC1) are translated here; plain ones are in gl_thunks.c.
 */
#include "icon_data.h"
#include "host.h"
#include "overlay.h"
#include "gl_state.h"
#include "pvrtc.h"

#include <SDL.h>
#include <stdlib.h>

#define FX(v) ((float)(int32_t)(v) / 65536.0f)
#define GLFN(type, name, ...)                                                                    \
    static type (*name)(__VA_ARGS__);                                                              \
    if (!name) {                                                                                   \
        *(void **)&name = gl_proc(#name);                                                          \
    }

enum {
    GL_EXTENSIONS_ENUM = 0x1f03,
    GL_VENDOR_ENUM = 0x1f00,
    GL_RENDERER_ENUM = 0x1f01,
    GL_VERSION_ENUM = 0x1f02,
    GL_SHADING_LANGUAGE_VERSION_ENUM = 0x8b8c,
    GL_NUM_COMPRESSED_TEXTURE_FORMATS_ENUM = 0x86a2,
    GL_COMPRESSED_TEXTURE_FORMATS_ENUM = 0x86a3,
    GL_ETC1_RGB8_OES_ENUM = 0x8d64,
    GL_COMPRESSED_RGB8_ETC2_ENUM = 0x9274,
    GL_MAX_TEXTURE_SIZE_ENUM = 0x0d33,
    GL_IMPLEMENTATION_COLOR_READ_TYPE_ENUM = 0x8b9a,
    GL_IMPLEMENTATION_COLOR_READ_FORMAT_ENUM = 0x8b9b,

    EGL_SUCCESS = 0x3000,
    EGL_BAD_ATTRIBUTE = 0x3004,
    EGL_BUFFER_SIZE = 0x3020,
    EGL_ALPHA_SIZE = 0x3021,
    EGL_BLUE_SIZE = 0x3022,
    EGL_GREEN_SIZE = 0x3023,
    EGL_RED_SIZE = 0x3024,
    EGL_DEPTH_SIZE = 0x3025,
    EGL_STENCIL_SIZE = 0x3026,
    EGL_CONFIG_CAVEAT = 0x3027,
    EGL_CONFIG_ID = 0x3028,
    EGL_SAMPLE_BUFFERS = 0x3032,
    EGL_SAMPLES = 0x3031,
    EGL_SURFACE_TYPE = 0x3033,
    EGL_NONE = 0x3038,
    EGL_RENDERABLE_TYPE = 0x3040,
    EGL_NATIVE_VISUAL_ID = 0x302e,
    EGL_VENDOR = 0x3053,
    EGL_VERSION = 0x3054,
    EGL_EXTENSIONS = 0x3055,
    EGL_HEIGHT = 0x3056,
    EGL_WIDTH = 0x3057,
    EGL_CLIENT_APIS = 0x308d,
    EGL_CONTEXT_CLIENT_TYPE = 0x3097,
    EGL_CONTEXT_CLIENT_VERSION = 0x3098,
    EGL_OPENGL_ES_API = 0x30a0,
    EGL_DRAW = 0x3059,
};

/* Opaque EGL handles: guest-visible tokens. */
static uint32_t g_egl_display, g_egl_config, g_egl_surface, g_egl_context;
static int g_es_version = 1;
static int g_current;

static SDL_Window *g_window;
static SDL_GLContext g_context;
static int g_swap_interval;

static void configure_swap_interval(void) {
    const char *override = getenv("CODBOZ_NO_VSYNC");
    bool force_off = override && strcmp(override, "0") != 0;
    int requested = force_off ? 0 : (g_opt.vsync ? 1 : 0);
    int result = SDL_GL_SetSwapInterval(requested);
    g_swap_interval = SDL_GL_GetSwapInterval();
    log_info("vsync: requested %d, reported %d, override %s, fps_limit %d%s%s",
             requested, g_swap_interval, force_off ? "off" : "none", g_opt.fps_limit,
             result ? ", SDL error: " : "", result ? SDL_GetError() : "");
}
/*
 * The game always renders into a 1280x720 framebuffer, the TV's resolution: the
 * Flash menus lay out differently at other sizes, and the reimagined layer (menu
 * probes, overlays, touch points) is measured in 1280x720. Each frame is scaled
 * into the window.
 */
static int g_width = 1280, g_height = 720;
static int g_win_w = 1280, g_win_h = 720;
/* What the framebuffer really is. The game sees g_width x g_height (its layout); on a fixed surface
 * its default-framebuffer coordinates are scaled up here, so it draws natively at this size. */
static int g_render_w = 1280, g_render_h = 720;
static uint32_t g_game_fbo, g_game_color, g_game_depth;

int video_width(void) {
    return g_width;
}

int video_height(void) {
    return g_height;
}

static void gl_missing(void) {
}

void *gl_proc(const char *name) {
    void *fn = SDL_GL_GetProcAddress(name);
    if (!fn) {
        char alt[128];
        snprintf(alt, sizeof(alt), "%sOES", name);
        fn = SDL_GL_GetProcAddress(alt);
    }
    if (!fn) {
        log_info("OpenGL entry point %s is unavailable", name);
        return (void *)(uintptr_t)gl_missing;
    }
    return fn;
}

bool video_init(void) {
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        log_error("SDL video: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    /* CODBOZ_HIDDEN: render without showing a window (screenshot checks) */
    uint32_t flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI |
                     (getenv("CODBOZ_HIDDEN") ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
    int width = g_opt.width, height = g_opt.height;
    /* the launcher runs in a window; video_enter_game() goes fullscreen */
    flags |= SDL_WINDOW_RESIZABLE;
    g_window = SDL_CreateWindow("Call of Duty: Black Ops Zombies", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, width, height, flags);
    if (!g_window) {
        log_error("SDL window: %s", SDL_GetError());
        return false;
    }
    {
        SDL_Surface *icon = SDL_CreateRGBSurfaceWithFormatFrom((void *)g_icon_rgba, ICON_W, ICON_H, 32,
                                                               ICON_W * 4, SDL_PIXELFORMAT_RGBA32);
        if (icon) {
            SDL_SetWindowIcon(g_window, icon);
            SDL_FreeSurface(icon);
        }
    }
    g_context = SDL_GL_CreateContext(g_window);
    if (!g_context) {
        log_error("OpenGL context: %s", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(g_window, g_context);
    configure_swap_interval();
    {
        int32_t units = 0, attributes = 0;
        void (*get_integer)(uint32_t, int32_t *) = gl_proc("glGetIntegerv");
        get_integer(0x8b4d, &units); /* GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS */
        get_integer(0x8869, &attributes); /* GL_MAX_VERTEX_ATTRIBS */
        const char *cache = getenv("CODBOZ_GL_CACHE");
        bool enabled = !cache || strcmp(cache, "0") != 0;
        gl_state_init(enabled, units > 0 ? (unsigned)units : 0,
                      attributes > 0 ? (unsigned)attributes : 0);
        const char *client = getenv("CODBOZ_GL_CLIENT_ARRAY_CACHE");
        bool client_enabled = !client || strcmp(client, "0") != 0;
        gl_state_client_arrays(client_enabled);
        log_info("GL state cache: %s (CODBOZ_GL_CACHE=0 disables)", enabled ? "on" : "off");
        log_info("GL client-array cache: %s (compatibility context, VAO zero only)",
                 enabled && client_enabled ? "on" : "off");
    }
    SDL_GL_GetDrawableSize(g_window, &g_width, &g_height);
    log_info("OpenGL %s on %s, drawable %dx%d",
             (const char *)((const uint8_t *(*)(uint32_t))gl_proc("glGetString"))(GL_VERSION_ENUM),
             (const char *)((const uint8_t *(*)(uint32_t))gl_proc("glGetString"))(GL_RENDERER_ENUM),
             g_width, g_height);

    g_egl_display = H2G(arena_alloc(16));
    g_egl_config = H2G(arena_alloc(16));
    g_egl_surface = H2G(arena_alloc(16));
    g_egl_context = H2G(arena_alloc(16));
    overlay_init();
    return true;
}

void video_shutdown(void) {
    gl_state_invalidate();
    if (g_context) {
        SDL_GL_DeleteContext(g_context);
        g_context = NULL;
    }
    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = NULL;
    }
}

/* Before the game boots: borderless fullscreen at the monitor's resolution, if set. */
/* The Reimagined menus are measured on the game's 1280x720 layout: at other sizes the game lays its
 * Flash pages out differently and nothing lines up. So the game keeps a 1280x720 logical surface and
 * draws at the chosen resolution through scaled viewports (see "logical surface" below). */
static bool g_fixed_surface;

bool video_surface_fixed(void) {
    return g_fixed_surface;
}

void video_fix_surface(void) {
    g_fixed_surface = true;
}

void video_enter_game(void) {
    if (g_opt.fullscreen) {
        SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
        SDL_SetWindowSize(g_window, g_opt.width, g_opt.height);
    }
    SDL_PumpEvents();
    configure_swap_interval(); /* Fullscreen transitions can reset swap policy. */
    SDL_GL_GetDrawableSize(g_window, &g_win_w, &g_win_h);
    g_render_w = g_width;
    g_render_h = g_height;
    if (g_fixed_surface) {
        /* other aspect ratios: 16:9 inside the chosen size, the window letterboxes it */
        if (g_render_w * 9 > g_render_h * 16) {
            g_render_w = g_render_h * 16 / 9;
        } else {
            g_render_h = g_render_w * 9 / 16;
        }
        g_width = 1280;
        g_height = 720;
    }

    GLFN(void, glGenFramebuffers, int32_t, uint32_t *);
    GLFN(void, glGenRenderbuffers, int32_t, uint32_t *);
    GLFN(void, glBindRenderbuffer, uint32_t, uint32_t);
    GLFN(void, glRenderbufferStorage, uint32_t, uint32_t, int32_t, int32_t);
    GLFN(void, glFramebufferRenderbuffer, uint32_t, uint32_t, uint32_t, uint32_t);
    GLFN(void, glBindFramebuffer, uint32_t, uint32_t);
    glGenFramebuffers(1, &g_game_fbo);
    glGenRenderbuffers(1, &g_game_color);
    glGenRenderbuffers(1, &g_game_depth);
    glBindRenderbuffer(0x8d41, g_game_color);
    glRenderbufferStorage(0x8d41, 0x8058, g_render_w, g_render_h); /* RGBA8 */
    glBindRenderbuffer(0x8d41, g_game_depth);
    glRenderbufferStorage(0x8d41, 0x88f0, g_render_w, g_render_h); /* DEPTH24_STENCIL8 */
    glBindRenderbuffer(0x8d41, 0);
    glBindFramebuffer(0x8d40, g_game_fbo);
    glFramebufferRenderbuffer(0x8d40, 0x8ce0, 0x8d41, g_game_color);
    glFramebufferRenderbuffer(0x8d40, 0x821a, 0x8d41, g_game_depth); /* DEPTH_STENCIL */
    log_info("game surface %dx%d, rendered at %dx%d in a %dx%d window", g_width, g_height, g_render_w, g_render_h, g_win_w, g_win_h);
}

int video_render_width(void) {
    return g_render_w;
}

int video_render_height(void) {
    return g_render_h;
}

uint32_t video_game_fbo(void) {
    return g_game_fbo;
}

/* Scale the game frame into the window, letterboxed. */
static void present_game(void) {
    GLFN(void, glBindFramebuffer, uint32_t, uint32_t);
    GLFN(void, glBlitFramebuffer, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
         int32_t, uint32_t, uint32_t);
    GLFN(uint8_t, glIsEnabled, uint32_t);
    GLFN(void, glDisable, uint32_t);
    GLFN(void, glEnable, uint32_t);
    GLFN(void, glClearColor, float, float, float, float);
    GLFN(void, glClear, uint32_t);
    GLFN(void, glColorMask, uint8_t, uint8_t, uint8_t, uint8_t);
    SDL_GL_GetDrawableSize(g_window, &g_win_w, &g_win_h);
    float scale = (float)g_win_w / g_render_w < (float)g_win_h / g_render_h ? (float)g_win_w / g_render_w
                                                                            : (float)g_win_h / g_render_h;
    int w = (int)(g_render_w * scale), h = (int)(g_render_h * scale);
    int x = (g_win_w - w) / 2, y = (g_win_h - h) / 2;
    uint8_t scissor = glIsEnabled(0x0c11);
    glDisable(0x0c11);
    glColorMask(1, 1, 1, 1);
    glBindFramebuffer(0x8ca9, 0);
    glClearColor(0, 0, 0, 1);
    glClear(0x4000);
    glBindFramebuffer(0x8ca8, g_game_fbo);
    glBlitFramebuffer(0, 0, g_render_w, g_render_h, x, y, x + w, y + h, 0x4000, 0x2601);
    glBindFramebuffer(0x8d40, 0);
    if (scissor) {
        glEnable(0x0c11);
    }
}

SDL_Window *video_window(void) {
    return g_window;
}

/* ---- logical surface ---- */

static bool g_default_bound = true;
static int g_viewport[4] = {0, 0, 1280, 720}, g_scissor[4] = {0, 0, 1280, 720};

static bool surface_scaled(void) {
    return g_default_bound && (g_render_w != g_width || g_render_h != g_height);
}

static void scale_rect(const int in[4], int out[4]) {
    int x1 = in[0] + in[2], y1 = in[1] + in[3];
    out[0] = (int)((int64_t)in[0] * g_render_w / g_width);
    out[1] = (int)((int64_t)in[1] * g_render_h / g_height);
    out[2] = (int)((int64_t)x1 * g_render_w / g_width) - out[0];
    out[3] = (int)((int64_t)y1 * g_render_h / g_height) - out[1];
}

static void logical_surface_restore(void) {
    GLFN(void, glViewport, int32_t, int32_t, int32_t, int32_t);
    GLFN(void, glScissor, int32_t, int32_t, int32_t, int32_t);
    int v[4], s[4];
    memcpy(v, g_viewport, sizeof(v));
    memcpy(s, g_scissor, sizeof(s));
    if (surface_scaled()) {
        scale_rect(g_viewport, v);
        scale_rect(g_scissor, s);
    }
    glViewport(v[0], v[1], v[2], v[3]);
    glScissor(s[0], s[1], s[2], s[3]);
}

IMPORT(glViewport) {
    for (int i = 0; i < 4; ++i) {
        g_viewport[i] = (int)arg(i);
    }
    logical_surface_restore();
}

IMPORT(glScissor) {
    for (int i = 0; i < 4; ++i) {
        g_scissor[i] = (int)arg(i);
    }
    logical_surface_restore();
}

IMPORT(glLineWidth) {
    GLFN(void, glLineWidth, float);
    glLineWidth(argf(0) * (surface_scaled() ? (float)g_render_h / (float)g_height : 1.0f));
}

/*
 * Reads from the default framebuffer: the game asked for logical pixels, so the region is resampled
 * to that size in a small target first and read from there. Returns true when it swapped the READ
 * binding; surface_read_end puts it back.
 */
static bool surface_read_begin(int *x, int *y, int w, int h) {
    static uint32_t fbo, rb;
    static int cap_w, cap_h;
    if (!surface_scaled() || w <= 0 || h <= 0) {
        return false;
    }
    GLFN(void, glGenFramebuffers, int32_t, uint32_t *);
    GLFN(void, glGenRenderbuffers, int32_t, uint32_t *);
    GLFN(void, glBindRenderbuffer, uint32_t, uint32_t);
    GLFN(void, glRenderbufferStorage, uint32_t, uint32_t, int32_t, int32_t);
    GLFN(void, glFramebufferRenderbuffer, uint32_t, uint32_t, uint32_t, uint32_t);
    GLFN(void, glBindFramebuffer, uint32_t, uint32_t);
    GLFN(void, glBlitFramebuffer, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t);
    GLFN(uint8_t, glIsEnabled, uint32_t);
    GLFN(void, glDisable, uint32_t);
    GLFN(void, glEnable, uint32_t);
    if (!fbo) {
        glGenFramebuffers(1, &fbo);
        glGenRenderbuffers(1, &rb);
    }
    if (w > cap_w || h > cap_h) {
        cap_w = w > cap_w ? w : cap_w;
        cap_h = h > cap_h ? h : cap_h;
        glBindRenderbuffer(0x8d41, rb);
        glRenderbufferStorage(0x8d41, 0x8058, cap_w, cap_h);
    }
    int in[4] = {*x, *y, w, h}, r[4];
    scale_rect(in, r);
    uint8_t scissor = glIsEnabled(0x0c11);
    glDisable(0x0c11);
    glBindFramebuffer(0x8ca9, fbo);
    glFramebufferRenderbuffer(0x8ca9, 0x8ce0, 0x8d41, rb);
    glBindFramebuffer(0x8ca8, g_game_fbo);
    glBlitFramebuffer(r[0], r[1], r[0] + r[2], r[1] + r[3], 0, 0, w, h, 0x4000, 0x2601);
    glBindFramebuffer(0x8ca9, g_game_fbo);
    glBindFramebuffer(0x8ca8, fbo);
    if (scissor) {
        glEnable(0x0c11);
    }
    *x = 0;
    *y = 0;
    return true;
}

static void surface_read_end(void) {
    GLFN(void, glBindFramebuffer, uint32_t, uint32_t);
    glBindFramebuffer(0x8ca8, g_game_fbo);
}

IMPORT(glReadPixels) {
    GLFN(void, glReadPixels, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void *);
    int x = (int)arg(0), y = (int)arg(1);
    bool swapped = surface_read_begin(&x, &y, (int)arg(2), (int)arg(3));
    glReadPixels(x, y, (int)arg(2), (int)arg(3), arg(4), arg(5), G2H(arg(6)));
    if (swapped) {
        surface_read_end();
    }
}

IMPORT(glCopyTexImage2D) {
    GLFN(void, glCopyTexImage2D, uint32_t, int32_t, uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
    int x = (int)arg(3), y = (int)arg(4);
    bool swapped = surface_read_begin(&x, &y, (int)arg(5), (int)arg(6));
    glCopyTexImage2D(arg(0), (int)arg(1), arg(2), x, y, (int)arg(5), (int)arg(6), (int)arg(7));
    if (swapped) {
        surface_read_end();
    }
}

IMPORT(glCopyTexSubImage2D) {
    GLFN(void, glCopyTexSubImage2D, uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
    int x = (int)arg(4), y = (int)arg(5);
    bool swapped = surface_read_begin(&x, &y, (int)arg(6), (int)arg(7));
    glCopyTexSubImage2D(arg(0), (int)arg(1), (int)arg(2), (int)arg(3), x, y, (int)arg(6), (int)arg(7));
    if (swapped) {
        surface_read_end();
    }
}

static void pace_frame(void) {
    static uint64_t next_us;
    if (g_opt.fps_limit <= 0) {
        return;
    }
    uint64_t interval = 1000000u / (uint64_t)g_opt.fps_limit;
    uint64_t now = monotonic_us();
    if (!next_us || now > next_us + interval * 4) {
        next_us = now;
    }
    while (now + 1500 < next_us) {
        sleep_ms((uint32_t)((next_us - now - 1000) / 1000u) ?: 1);
        now = monotonic_us();
    }
    while (now < next_us) {
        now = monotonic_us();
    }
    next_us += interval;
}

/* Debug aid: CODBOZ_SHOT_DIR=dir saves a PPM every CODBOZ_SHOT_EVERY frames (default 300). */
static char g_shot_request[1024];

/* Saves the next presented frame to path (PPM); used for one-off diagnostics. */
void video_request_shot(const char *path) {
    snprintf(g_shot_request, sizeof(g_shot_request), "%s", path);
}

void video_debug_capture(void) {
    static const char *dir;
    static int every, frame, checked;
    if (!checked) {
        checked = 1;
        dir = getenv("CODBOZ_SHOT_DIR");
        every = getenv("CODBOZ_SHOT_EVERY") ? atoi(getenv("CODBOZ_SHOT_EVERY")) : 300;
    }
    bool requested = g_shot_request[0] != 0;
    if (!requested && (!dir || every <= 0 || ++frame % every)) {
        return;
    }
    GLFN(void, glReadPixels, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void *);
    GLFN(void, glBindFramebuffer, uint32_t, uint32_t);
    GLFN(void, glPixelStorei, uint32_t, int32_t);
    int ww = g_win_w, wh = g_win_h;
    uint8_t *pixels = malloc((size_t)ww * wh * 3);
    glBindFramebuffer(0x8d40, 0);
    glPixelStorei(0x0d05, 1); /* GL_PACK_ALIGNMENT */
    glReadPixels(0, 0, ww, wh, 0x1907, 0x1401, pixels);
    char path[1200];
    if (requested) {
        snprintf(path, sizeof(path), "%s", g_shot_request);
        g_shot_request[0] = 0;
    } else {
        snprintf(path, sizeof(path), "%s/frame%05d.ppm", dir, frame);
    }
    FILE *file = fopen(path, "wb");
    if (file) {
        fprintf(file, "P6\n%d %d\n255\n", ww, wh);
        for (int y = wh - 1; y >= 0; --y) {
            fwrite(pixels + (size_t)y * ww * 3, 1, (size_t)ww * 3, file);
        }
        fclose(file);
        log_info("saved %s", path);
    }
    free(pixels);
}

/*
 * FPS limit. Below 60 the game's frame keeps ticking at 60 Hz, so simulation, network and input run as
 * at 60 fps; only presenting is rarer, and frames that will not be shown drop their draw calls.
 * At 60 and above the limit paces the whole frame, as the launcher setting always did.
 */
extern bool gl_skip_draws;

static int effective_fps_limit(void) {
    extern bool reimagined_match_waiting(void);
    if (g_opt.low_fps_paused > 0 && reimagined_match_waiting()) {
        return g_opt.low_fps_paused;
    }
    return g_opt.fps_limit;
}

static void sleep_until(uint64_t *next, uint64_t interval) {
    uint64_t now = monotonic_us();
    if (!*next || now > *next + interval * 4) {
        *next = now;
    }
    while (now + 1000 < *next) {
        sleep_ms((uint32_t)((*next - now) / 1000u) ?: 1);
        now = monotonic_us();
    }
    while (now < *next) { /* under 1 ms */
        now = monotonic_us();
    }
    *next += interval;
}

void video_swap(void) {
    /* The presentation/HUD code uses native GL directly, outside guest thunks. */
    gl_state_invalidate();
    GLFN(void, glBindFramebuffer, uint32_t, uint32_t);
    extern void overlay_logic(void);
    int limit = effective_fps_limit();
    static uint64_t next_present, next_tick;
    bool slow = limit > 0 && limit < 60;
    uint64_t now = monotonic_us();
    bool present = !slow || now >= next_present;
    uint64_t swap0 = now;
    if (present) {
        if (slow) {
            uint64_t interval = 1000000u / (uint64_t)limit;
            next_present = now > next_present + interval ? now + interval : next_present + interval;
        }
        if (g_game_fbo) {
            present_game();
        }
        overlay_draw(g_win_w, g_win_h);
        video_debug_capture();
        swap0 = monotonic_us();
        SDL_GL_SwapWindow(g_window);
        if (g_game_fbo) {
            glBindFramebuffer(0x8d40, g_game_fbo);
        }
    } else {
        overlay_logic();
    }
    if (slow) {
        sleep_until(&next_tick, 1000000u / 60u);
        /* draw only the frame that will be presented next */
        gl_skip_draws = monotonic_us() + 1000000u / 60u < next_present;
    } else {
        gl_skip_draws = false;
        if (g_swap_interval == 0) {
            pace_frame();
        }
    }
    g_perf.swap_us += monotonic_us() - swap0;
    input_after_swap();
    gl_state_invalidate();
    logical_surface_restore();
    timer_frame_tick();
}

/* ---- EGL ---- */

static uint32_t g_egl_error = EGL_SUCCESS;

IMPORT(eglGetDisplay) {
    ret(g_egl_display);
}

IMPORT(eglInitialize) {
    if (arg(1)) {
        *(int32_t *)G2H(arg(1)) = 1;
    }
    if (arg(2)) {
        *(int32_t *)G2H(arg(2)) = 4;
    }
    ret(1);
}

IMPORT(eglTerminate) {
    ret(1);
}

IMPORT(eglBindAPI) {
    ret(arg(0) == EGL_OPENGL_ES_API);
}

IMPORT(eglGetError) {
    uint32_t error = g_egl_error;
    g_egl_error = EGL_SUCCESS;
    ret(error);
}

IMPORT(eglQueryString) {
    static uint32_t vendor, version, extensions, apis;
    if (!vendor) {
        vendor = arena_strdup("codboz-pc");
        version = arena_strdup("1.4 codboz-pc");
        extensions = arena_strdup("EGL_KHR_image_base");
        apis = arena_strdup("OpenGL_ES");
    }
    switch (arg(1)) {
    case EGL_VENDOR:
        ret(vendor);
        break;
    case EGL_VERSION:
        ret(version);
        break;
    case EGL_EXTENSIONS:
        ret(extensions);
        break;
    case EGL_CLIENT_APIS:
        ret(apis);
        break;
    default:
        ret(0);
        break;
    }
}

IMPORT(eglGetConfigs) {
    uint32_t configs = arg(1);
    int32_t size = (int32_t)arg(2);
    if (arg(3)) {
        *(int32_t *)G2H(arg(3)) = 1;
    }
    if (configs && size > 0) {
        *(uint32_t *)G2H(configs) = g_egl_config;
    }
    ret(1);
}

IMPORT(eglGetConfigAttrib) {
    int32_t value;
    switch (arg(2)) {
    case EGL_RED_SIZE:
    case EGL_GREEN_SIZE:
    case EGL_BLUE_SIZE:
    case EGL_ALPHA_SIZE:
        value = 8;
        break;
    case EGL_BUFFER_SIZE:
        value = 32;
        break;
    case EGL_DEPTH_SIZE:
        value = 24;
        break;
    case EGL_STENCIL_SIZE:
        value = 8;
        break;
    case EGL_CONFIG_ID:
        value = 1;
        break;
    case EGL_SURFACE_TYPE:
        value = 0x0005; /* window | pbuffer */
        break;
    case EGL_RENDERABLE_TYPE:
        value = 0x0005; /* ES1 | ES2 */
        break;
    case EGL_CONFIG_CAVEAT:
        value = EGL_NONE;
        break;
    case EGL_SAMPLES:
    case EGL_SAMPLE_BUFFERS:
    case EGL_NATIVE_VISUAL_ID:
        value = 0;
        break;
    default:
        g_egl_error = EGL_BAD_ATTRIBUTE;
        ret(0);
        return;
    }
    if (arg(3)) {
        *(int32_t *)G2H(arg(3)) = value;
    }
    ret(1);
}

IMPORT(eglCreateWindowSurface) {
    ret(g_egl_surface);
}

IMPORT(eglCreatePbufferSurface) {
    ret(H2G(arena_alloc(16)));
}

IMPORT(eglDestroySurface) {
    ret(1);
}

IMPORT(eglCreateContext) {
    const int32_t *attributes = G2H(arg(3));
    for (int i = 0; attributes && i < 32 && attributes[i * 2] != EGL_NONE; ++i) {
        if (attributes[i * 2] == EGL_CONTEXT_CLIENT_VERSION) {
            g_es_version = attributes[i * 2 + 1];
        }
    }
    log_info("game requested an OpenGL ES %d context", g_es_version);
    ret(g_egl_context);
}

IMPORT(eglDestroyContext) {
    ret(1);
}

IMPORT(eglMakeCurrent) {
    gl_state_invalidate();
    g_current = arg(3) != 0;
    ret(1);
}

IMPORT(eglGetCurrentContext) {
    ret(g_current ? g_egl_context : 0);
}

IMPORT(eglGetCurrentDisplay) {
    ret(g_current ? g_egl_display : 0);
}

IMPORT(eglGetCurrentSurface) {
    ret(g_current ? g_egl_surface : 0);
}

IMPORT(eglQuerySurface) {
    int32_t value = 0;
    if (arg(2) == EGL_WIDTH) {
        value = g_width;
    } else if (arg(2) == EGL_HEIGHT) {
        value = g_height;
    } else if (arg(2) == EGL_CONFIG_ID) {
        value = 1;
    }
    if (arg(3)) {
        *(int32_t *)G2H(arg(3)) = value;
    }
    ret(1);
}

IMPORT(eglQueryContext) {
    int32_t value = 0;
    if (arg(2) == EGL_CONTEXT_CLIENT_VERSION) {
        value = g_es_version;
    } else if (arg(2) == EGL_CONTEXT_CLIENT_TYPE) {
        value = EGL_OPENGL_ES_API;
    } else if (arg(2) == EGL_CONFIG_ID) {
        value = 1;
    }
    if (arg(3)) {
        *(int32_t *)G2H(arg(3)) = value;
    }
    ret(1);
}

IMPORT(eglSwapBuffers) {
    video_swap();
    ret(1);
}

IMPORT(eglBindTexImage) {
    ret(0);
}

IMPORT(eglReleaseTexImage) {
    ret(0);
}

IMPORT(eglGetProcAddress) {
    char name[128];
    snprintf(name, sizeof(name), "%s", (const char *)G2H(arg(0)));
    size_t len = strlen(name);
    if (len > 3 && strcmp(name + len - 3, "OES") == 0) {
        name[len - 3] = 0;
    }
    /* Only hand out entry points we can translate. */
    extern int gl_has_thunk(const char *name);
    if (!gl_has_thunk(name)) {
        log_debug("eglGetProcAddress(%s): unavailable", (const char *)G2H(arg(0)));
        ret(0);
        return;
    }
    ret(import_resolve(name));
}

/* ---- OpenGL ES translations ---- */


static uint32_t g_gl_strings[5];

IMPORT(glGetString) {
    GLFN(const char *, glGetString, uint32_t);
    uint32_t name = arg(0);
    int slot;
    char buffer[8192];
    switch (name) {
    case GL_VENDOR_ENUM:
        slot = 0;
        snprintf(buffer, sizeof(buffer), "%s", glGetString(name));
        break;
    case GL_RENDERER_ENUM:
        slot = 1;
        snprintf(buffer, sizeof(buffer), "%s", glGetString(name));
        break;
    case GL_VERSION_ENUM:
        slot = 2;
        snprintf(buffer, sizeof(buffer), g_es_version >= 2 ? "OpenGL ES 2.0 codboz-pc"
                                                           : "OpenGL ES-CM 1.1 codboz-pc");
        break;
    case GL_SHADING_LANGUAGE_VERSION_ENUM:
        slot = 3;
        snprintf(buffer, sizeof(buffer), "OpenGL ES GLSL ES 1.00");
        break;
    case GL_EXTENSIONS_ENUM:
        slot = 4;
        snprintf(buffer, sizeof(buffer),
                 "%s"
                 "GL_OES_compressed_ETC1_RGB8_texture GL_OES_framebuffer_object "
                 "GL_OES_depth24 GL_OES_packed_depth_stencil GL_OES_rgb8_rgba8 "
                 "GL_OES_texture_npot GL_OES_element_index_uint GL_OES_blend_subtract "
                 "GL_OES_stencil8 GL_OES_vertex_half_float GL_EXT_texture_format_BGRA8888 "
                 "GL_IMG_texture_npot GL_OES_mapbuffer_disabled",
                 g_pvrtc_textures ? "GL_IMG_texture_compression_pvrtc " : "");
        break;
    default:
        ret(0);
        return;
    }
    if (!g_gl_strings[slot]) {
        g_gl_strings[slot] = arena_strdup(buffer);
    }
    ret(g_gl_strings[slot]);
}

IMPORT(glGetIntegerv) {
    GLFN(void, glGetIntegerv, uint32_t, int32_t *);
    int32_t *out = G2H(arg(1));
    switch (arg(0)) {
    case 0x0ba2: /* VIEWPORT: what the game set, in its own coordinates */
        memcpy(out, g_viewport, sizeof(g_viewport));
        return;
    case 0x0c10: /* SCISSOR_BOX */
        memcpy(out, g_scissor, sizeof(g_scissor));
        return;
    case GL_NUM_COMPRESSED_TEXTURE_FORMATS_ENUM:
        out[0] = g_pvrtc_textures ? 5 : 1;
        return;
    case GL_COMPRESSED_TEXTURE_FORMATS_ENUM:
        out[0] = GL_ETC1_RGB8_OES_ENUM;
        if (g_pvrtc_textures) {
            for (int i = 0; i < 4; ++i) out[1 + i] = PVRTC_RGB_4BPP + i;
        }
        return;
    case GL_IMPLEMENTATION_COLOR_READ_TYPE_ENUM:
        out[0] = 0x1401; /* GL_UNSIGNED_BYTE */
        return;
    case GL_IMPLEMENTATION_COLOR_READ_FORMAT_ENUM:
        out[0] = 0x1908; /* GL_RGBA */
        return;
    case 0x8ca6: /* FRAMEBUFFER_BINDING: the game's "0" is our 1280x720 target */
    case 0x8caa:
        glGetIntegerv(arg(0), out);
        if ((uint32_t)out[0] == g_game_fbo) {
            out[0] = 0;
        }
        return;
    default:
        glGetIntegerv(arg(0), out);
        return;
    }
}

IMPORT(glBindFramebuffer) {
    GLFN(void, glBindFramebuffer, uint32_t, uint32_t);
    glBindFramebuffer(arg(0), arg(1) ? arg(1) : g_game_fbo);
    if (arg(0) != 0x8ca8 && g_default_bound != (arg(1) == 0)) { /* not READ only */
        g_default_bound = arg(1) == 0;
        logical_surface_restore();
    }
}

static uint32_t remap_compressed(uint32_t format) {
    static uint32_t seen[8];
    for (int i = 0; i < 8; ++i) {
        if (seen[i] == format) break;
        if (!seen[i]) {
            seen[i] = format;
            log_info("compressed texture format 0x%04x%s", format,
                     format == GL_ETC1_RGB8_OES_ENUM ? " (ETC1 -> ETC2)" : "");
            break;
        }
    }
    return format == GL_ETC1_RGB8_OES_ENUM ? GL_COMPRESSED_RGB8_ETC2_ENUM : format;
}

/* GLES lets internalformat be GL_BGRA_EXT (EXT/APPLE_texture_format_BGRA8888, used by the iOS
 * build); desktop GL rejects it, so the texture would stay empty. Store it as RGBA8 instead and
 * keep the BGRA client data, which desktop GL converts on upload. */
IMPORT(glTexImage2D) {
    GLFN(void, glTexImage2D, uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t,
         const void *);
    int32_t internal = (int32_t)arg(2);
    uint32_t format = arg(6), type = arg(7);
    static uint32_t seen[16];
    uint32_t key = ((uint32_t)internal & 0xffffu) << 16 ^ (format & 0xffffu) ^ (type << 4);
    for (int i = 0; i < 16; ++i) {
        if (seen[i] == key) break;
        if (!seen[i]) {
            seen[i] = key;
            log_debug("glTexImage2D internal 0x%04x format 0x%04x type 0x%04x", (unsigned)internal,
                      format, type);
            break;
        }
    }
    if (internal == 0x80e1 /* GL_BGRA_EXT */) {
        internal = 0x8058; /* GL_RGBA8 */
    }
    glTexImage2D(arg(0), (int32_t)arg(1), internal, (int32_t)arg(3), (int32_t)arg(4), (int32_t)arg(5),
                 format, type, G2H(arg(8)));
}

/* PVRTC (iOS data): decode to RGBA8; desktop GL has no PVRTC. */
static bool upload_pvrtc(bool sub, uint32_t target, int32_t level, int32_t x, int32_t y, int32_t w,
                         int32_t h, uint32_t format, int32_t size, const void *data) {
    if (!pvrtc_is_format(format)) {
        return false;
    }
    int bpp2 = format == PVRTC_RGB_2BPP || format == PVRTC_RGBA_2BPP;
    uint8_t *rgba = malloc((size_t)w * (size_t)h * 4u + 4u);
    if (!rgba || !pvrtc_decode(data, (size_t)size, w, h, bpp2, rgba)) {
        log_error("PVRTC %dx%d (format 0x%04x, %d bytes) could not be decoded", w, h, format, size);
        free(rgba);
        return true; /* nothing sensible to upload */
    }
    if (sub) {
        GLFN(void, glTexSubImage2D, uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t,
             uint32_t, const void *);
        glTexSubImage2D(target, level, x, y, w, h, 0x1908, 0x1401, rgba);
    } else {
        GLFN(void, glTexImage2D, uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t,
             uint32_t, const void *);
        glTexImage2D(target, level, 0x8058 /* GL_RGBA8 */, w, h, 0, 0x1908, 0x1401, rgba);
    }
    free(rgba);
    return true;
}

IMPORT(glCompressedTexImage2D) {
    GLFN(void, glCompressedTexImage2D, uint32_t, int32_t, uint32_t, int32_t, int32_t, int32_t,
         int32_t, const void *);
    uint32_t format = remap_compressed(arg(2));
    if (upload_pvrtc(false, arg(0), (int32_t)arg(1), 0, 0, (int32_t)arg(3), (int32_t)arg(4), format,
                     (int32_t)arg(6), G2H(arg(7)))) {
        return;
    }
    glCompressedTexImage2D(arg(0), (int32_t)arg(1), format, (int32_t)arg(3), (int32_t)arg(4),
                           (int32_t)arg(5), (int32_t)arg(6), G2H(arg(7)));
}

IMPORT(glCompressedTexSubImage2D) {
    GLFN(void, glCompressedTexSubImage2D, uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
         uint32_t, int32_t, const void *);
    uint32_t format = remap_compressed(arg(6));
    if (upload_pvrtc(true, arg(0), (int32_t)arg(1), (int32_t)arg(2), (int32_t)arg(3), (int32_t)arg(4),
                     (int32_t)arg(5), format, (int32_t)arg(7), G2H(arg(8)))) {
        return;
    }
    glCompressedTexSubImage2D(arg(0), (int32_t)arg(1), (int32_t)arg(2), (int32_t)arg(3),
                              (int32_t)arg(4), (int32_t)arg(5), format, (int32_t)arg(7), G2H(arg(8)));
}

IMPORT(glShaderSource) {
    GLFN(void, glShaderSource, uint32_t, int32_t, const char *const *, const int32_t *);
    int32_t count = (int32_t)arg(1);
    const uint32_t *strings = G2H(arg(2));
    const int32_t *lengths = G2H(arg(3));
    if (count <= 0) {
        return;
    }
    /* GLSL ES 1.00 -> desktop GLSL 1.20: one buffer, precision statements dropped. */
    static const char prelude[] = "#version 120\n#define lowp\n#define mediump\n#define highp\n";
    size_t total = sizeof(prelude);
    for (int32_t i = 0; i < count; ++i) {
        const char *src = G2H(strings[i]);
        total += lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(src);
    }
    char *text = malloc(total + 1);
    size_t at = 0;
    memcpy(text, prelude, sizeof(prelude) - 1);
    at = sizeof(prelude) - 1;
    for (int32_t i = 0; i < count; ++i) {
        const char *src = G2H(strings[i]);
        size_t len = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(src);
        memcpy(text + at, src, len);
        at += len;
    }
    text[at] = 0;
    for (char *p = text + sizeof(prelude) - 1; (p = strstr(p, "precision ")) != NULL;) {
        char *semicolon = strchr(p, ';');
        if (!semicolon) {
            break;
        }
        memset(p, ' ', (size_t)(semicolon - p + 1));
    }
    for (char *p = text + sizeof(prelude) - 1; (p = strstr(p, "#version")) != NULL;) {
        p[0] = '/';
        p[1] = '/';
    }
    log_debug("shader %u source:\n%s", arg(0), text);
    const char *one = text;
    glShaderSource(arg(0), 1, &one, NULL);
    free(text);
}

IMPORT(glCompileShader) {
    GLFN(void, glCompileShader, uint32_t);
    GLFN(void, glGetShaderiv, uint32_t, uint32_t, int32_t *);
    GLFN(void, glGetShaderInfoLog, uint32_t, int32_t, int32_t *, char *);
    glCompileShader(arg(0));
    int32_t ok = 0;
    glGetShaderiv(arg(0), 0x8b81, &ok); /* GL_COMPILE_STATUS */
    if (!ok) {
        char text[2048] = "";
        glGetShaderInfoLog(arg(0), sizeof(text), NULL, text);
        log_info("shader %u failed to compile: %s", arg(0), text);
    }
}

IMPORT(glTransformFeedbackVaryings) {
    GLFN(void, glTransformFeedbackVaryings, uint32_t, int32_t, const char *const *, uint32_t);
    int32_t count = (int32_t)arg(1);
    const uint32_t *strings = G2H(arg(2));
    const char *host[64];
    if (count > 64) {
        count = 64;
    }
    for (int32_t i = 0; i < count; ++i) {
        host[i] = G2H(strings[i]);
    }
    glTransformFeedbackVaryings(arg(0), count, host, arg(3));
}

IMPORT(glOrthof) {
    GLFN(void, glOrtho, double, double, double, double, double, double);
    glOrtho(argf(0), argf(1), argf(2), argf(3), argf(4), argf(5));
}

IMPORT(glOrthox) {
    GLFN(void, glOrtho, double, double, double, double, double, double);
    glOrtho(FX(arg(0)), FX(arg(1)), FX(arg(2)), FX(arg(3)), FX(arg(4)), FX(arg(5)));
}

IMPORT(glFrustumf) {
    GLFN(void, glFrustum, double, double, double, double, double, double);
    glFrustum(argf(0), argf(1), argf(2), argf(3), argf(4), argf(5));
}

IMPORT(glClipPlanef) {
    GLFN(void, glClipPlane, uint32_t, const double *);
    const float *eq = G2H(arg(1));
    double d[4] = {eq[0], eq[1], eq[2], eq[3]};
    glClipPlane(arg(0), d);
}

IMPORT(glClipPlanex) {
    GLFN(void, glClipPlane, uint32_t, const double *);
    const int32_t *eq = G2H(arg(1));
    double d[4] = {FX(eq[0]), FX(eq[1]), FX(eq[2]), FX(eq[3])};
    glClipPlane(arg(0), d);
}

IMPORT(glClearColorx) {
    GLFN(void, glClearColor, float, float, float, float);
    glClearColor(FX(arg(0)), FX(arg(1)), FX(arg(2)), FX(arg(3)));
}

IMPORT(glClearDepthx) {
    GLFN(void, glClearDepth, double);
    glClearDepth(FX(arg(0)));
}

IMPORT(glDepthRangex) {
    GLFN(void, glDepthRange, double, double);
    glDepthRange(FX(arg(0)), FX(arg(1)));
}

IMPORT(glAlphaFuncx) {
    GLFN(void, glAlphaFunc, uint32_t, float);
    glAlphaFunc(arg(0), FX(arg(1)));
}

IMPORT(glColor4x) {
    GLFN(void, glColor4f, float, float, float, float);
    glColor4f(FX(arg(0)), FX(arg(1)), FX(arg(2)), FX(arg(3)));
}

IMPORT(glNormal3x) {
    GLFN(void, glNormal3f, float, float, float);
    glNormal3f(FX(arg(0)), FX(arg(1)), FX(arg(2)));
}

IMPORT(glMultiTexCoord4x) {
    GLFN(void, glMultiTexCoord4f, uint32_t, float, float, float, float);
    glMultiTexCoord4f(arg(0), FX(arg(1)), FX(arg(2)), FX(arg(3)), FX(arg(4)));
}

IMPORT(glTranslatex) {
    GLFN(void, glTranslatef, float, float, float);
    glTranslatef(FX(arg(0)), FX(arg(1)), FX(arg(2)));
}

IMPORT(glScalex) {
    GLFN(void, glScalef, float, float, float);
    glScalef(FX(arg(0)), FX(arg(1)), FX(arg(2)));
}

IMPORT(glRotatex) {
    GLFN(void, glRotatef, float, float, float, float);
    glRotatef(FX(arg(0)), FX(arg(1)), FX(arg(2)), FX(arg(3)));
}

IMPORT(glLineWidthx) {
    GLFN(void, glLineWidth, float);
    glLineWidth(FX(arg(0)));
}

IMPORT(glPolygonOffsetx) {
    GLFN(void, glPolygonOffset, float, float);
    glPolygonOffset(FX(arg(0)), FX(arg(1)));
}

static void fixed_matrix(const int32_t *in, float *out) {
    for (int i = 0; i < 16; ++i) {
        out[i] = FX(in[i]);
    }
}

IMPORT(glLoadMatrixx) {
    GLFN(void, glLoadMatrixf, const float *);
    float m[16];
    fixed_matrix(G2H(arg(0)), m);
    glLoadMatrixf(m);
}

IMPORT(glMultMatrixx) {
    GLFN(void, glMultMatrixf, const float *);
    float m[16];
    fixed_matrix(G2H(arg(0)), m);
    glMultMatrixf(m);
}

/* Enum-valued parameters stay integers; everything else is 16.16. */
static int is_enum_param(uint32_t pname) {
    switch (pname) {
    case 0x0b65: /* GL_FOG_MODE */
    case 0x2200: /* GL_TEXTURE_ENV_MODE */
    case 0x8571: /* GL_COMBINE_RGB */
    case 0x8572: /* GL_COMBINE_ALPHA */
    case 0x8580: case 0x8581: case 0x8582: /* SRC0..2_RGB */
    case 0x8588: case 0x8589: case 0x858a: /* SRC0..2_ALPHA */
    case 0x8590: case 0x8591: case 0x8592: /* OPERAND0..2_RGB */
    case 0x8598: case 0x8599: case 0x859a: /* OPERAND0..2_ALPHA */
    case 0x2801: case 0x2800: case 0x2802: case 0x2803: /* texture filters/wraps */
    case 0x8191: /* GL_GENERATE_MIPMAP */
    case 0x8862: /* GL_COORD_REPLACE */
    case 0x0b52: /* GL_LIGHT_MODEL_TWO_SIDE */
        return 1;
    default:
        return 0;
    }
}

IMPORT(glFogx) {
    GLFN(void, glFogf, uint32_t, float);
    glFogf(arg(0), is_enum_param(arg(0)) ? (float)arg(1) : FX(arg(1)));
}

IMPORT(glFogxv) {
    GLFN(void, glFogfv, uint32_t, const float *);
    const int32_t *in = G2H(arg(1));
    float v[4];
    int n = arg(0) == 0x0b66 ? 4 : 1; /* GL_FOG_COLOR */
    for (int i = 0; i < n; ++i) {
        v[i] = is_enum_param(arg(0)) ? (float)in[i] : FX(in[i]);
    }
    glFogfv(arg(0), v);
}

IMPORT(glLightModelx) {
    GLFN(void, glLightModelf, uint32_t, float);
    glLightModelf(arg(0), is_enum_param(arg(0)) ? (float)arg(1) : FX(arg(1)));
}

IMPORT(glLightModelxv) {
    GLFN(void, glLightModelfv, uint32_t, const float *);
    const int32_t *in = G2H(arg(1));
    float v[4];
    int n = arg(0) == 0x0b53 ? 4 : 1; /* GL_LIGHT_MODEL_AMBIENT */
    for (int i = 0; i < n; ++i) {
        v[i] = is_enum_param(arg(0)) ? (float)in[i] : FX(in[i]);
    }
    glLightModelfv(arg(0), v);
}

static int light_param_count(uint32_t pname) {
    switch (pname) {
    case 0x1200: case 0x1201: case 0x1202: case 0x1203: /* AMBIENT DIFFUSE SPECULAR POSITION */
    case 0x1600: /* EMISSION */
    case 0x1602: /* AMBIENT_AND_DIFFUSE */
        return 4;
    case 0x1204: /* SPOT_DIRECTION */
        return 3;
    default:
        return 1;
    }
}

IMPORT(glLightx) {
    GLFN(void, glLightf, uint32_t, uint32_t, float);
    glLightf(arg(0), arg(1), FX(arg(2)));
}

IMPORT(glLightxv) {
    GLFN(void, glLightfv, uint32_t, uint32_t, const float *);
    const int32_t *in = G2H(arg(2));
    float v[4];
    for (int i = 0; i < light_param_count(arg(1)); ++i) {
        v[i] = FX(in[i]);
    }
    glLightfv(arg(0), arg(1), v);
}

IMPORT(glMaterialx) {
    GLFN(void, glMaterialf, uint32_t, uint32_t, float);
    glMaterialf(arg(0), arg(1), FX(arg(2)));
}

IMPORT(glMaterialxv) {
    GLFN(void, glMaterialfv, uint32_t, uint32_t, const float *);
    const int32_t *in = G2H(arg(2));
    float v[4];
    for (int i = 0; i < light_param_count(arg(1)); ++i) {
        v[i] = FX(in[i]);
    }
    glMaterialfv(arg(0), arg(1), v);
}

IMPORT(glTexEnvx) {
    GLFN(void, glTexEnvf, uint32_t, uint32_t, float);
    glTexEnvf(arg(0), arg(1), is_enum_param(arg(1)) ? (float)arg(2) : FX(arg(2)));
}

IMPORT(glTexEnvxv) {
    GLFN(void, glTexEnvfv, uint32_t, uint32_t, const float *);
    const int32_t *in = G2H(arg(2));
    float v[4];
    int n = arg(1) == 0x2201 ? 4 : 1; /* GL_TEXTURE_ENV_COLOR */
    for (int i = 0; i < n; ++i) {
        v[i] = is_enum_param(arg(1)) ? (float)in[i] : FX(in[i]);
    }
    glTexEnvfv(arg(0), arg(1), v);
}

IMPORT(glTexParameterx) {
    GLFN(void, glTexParameterf, uint32_t, uint32_t, float);
    glTexParameterf(arg(0), arg(1), is_enum_param(arg(1)) ? (float)arg(2) : FX(arg(2)));
}

IMPORT(glTexParameterxv) {
    GLFN(void, glTexParameterf, uint32_t, uint32_t, float);
    const int32_t *in = G2H(arg(2));
    glTexParameterf(arg(0), arg(1), is_enum_param(arg(1)) ? (float)in[0] : FX(in[0]));
}

void register_gl_thunks(void);

static const char *const k_special[] = {
    "glGetString", "glGetIntegerv", "glBindFramebuffer", "glViewport", "glScissor", "glLineWidth", "glReadPixels", "glCopyTexImage2D", "glCopyTexSubImage2D", "glTexImage2D", "glCompressedTexImage2D", "glCompressedTexSubImage2D",
    "glShaderSource", "glCompileShader", "glTransformFeedbackVaryings", "glOrthof", "glOrthox", "glFrustumf",
    "glClipPlanef", "glClipPlanex", "glClearColorx", "glClearDepthx", "glDepthRangex",
    "glAlphaFuncx", "glColor4x", "glNormal3x", "glMultiTexCoord4x", "glTranslatex", "glScalex",
    "glRotatex", "glLineWidthx", "glPolygonOffsetx", "glLoadMatrixx", "glMultMatrixx", "glFogx",
    "glFogxv", "glLightModelx", "glLightModelxv", "glLightx", "glLightxv", "glMaterialx",
    "glMaterialxv", "glTexEnvx", "glTexEnvxv", "glTexParameterx", "glTexParameterxv",
};

int gl_has_thunk(const char *name) {
    extern int gl_thunk_known(const char *name);
    for (size_t i = 0; i < sizeof(k_special) / sizeof(k_special[0]); ++i) {
        if (strcmp(k_special[i], name) == 0) {
            return 1;
        }
    }
    return gl_thunk_known(name);
}

void register_gl_imports(void) {
    register_gl_thunks();
    REG(glGetString);
    REG(glGetIntegerv);
    REG(glBindFramebuffer);
    REG(glViewport);
    REG(glScissor);
    REG(glLineWidth);
    REG(glReadPixels);
    REG(glCopyTexImage2D);
    REG(glCopyTexSubImage2D);
    REG(glTexImage2D);
    REG(glCompressedTexImage2D);
    REG(glCompressedTexSubImage2D);
    REG(glShaderSource);
    REG(glCompileShader);
    REG(glTransformFeedbackVaryings);
    REG(glOrthof);
    REG(glOrthox);
    REG(glFrustumf);
    REG(glClipPlanef);
    REG(glClipPlanex);
    REG(glClearColorx);
    REG(glClearDepthx);
    REG(glDepthRangex);
    REG(glAlphaFuncx);
    REG(glColor4x);
    REG(glNormal3x);
    REG(glMultiTexCoord4x);
    REG(glTranslatex);
    REG(glScalex);
    REG(glRotatex);
    REG(glLineWidthx);
    REG(glPolygonOffsetx);
    REG(glLoadMatrixx);
    REG(glMultMatrixx);
    REG(glFogx);
    REG(glFogxv);
    REG(glLightModelx);
    REG(glLightModelxv);
    REG(glLightx);
    REG(glLightxv);
    REG(glMaterialx);
    REG(glMaterialxv);
    REG(glTexEnvx);
    REG(glTexEnvxv);
    REG(glTexParameterx);
    REG(glTexParameterxv);
}

void register_egl_imports(void) {
    REG(eglGetDisplay);
    REG(eglInitialize);
    REG(eglTerminate);
    REG(eglBindAPI);
    REG(eglGetError);
    REG(eglQueryString);
    REG(eglGetConfigs);
    REG(eglGetConfigAttrib);
    REG(eglCreateWindowSurface);
    REG(eglCreatePbufferSurface);
    REG(eglDestroySurface);
    REG(eglCreateContext);
    REG(eglDestroyContext);
    REG(eglMakeCurrent);
    REG(eglGetCurrentContext);
    REG(eglGetCurrentDisplay);
    REG(eglGetCurrentSurface);
    REG(eglQuerySurface);
    REG(eglQueryContext);
    REG(eglSwapBuffers);
    REG(eglBindTexImage);
    REG(eglReleaseTexImage);
    REG(eglGetProcAddress);
}
