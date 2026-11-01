/* Everything the host draws over the game each frame. */
#include <stdlib.h>
#include <string.h>
#include "emu.h"
#include "overlay.h"
#include "host.h"
#include "menus.h"
#include "reimagined.h"
#include "screens.h"
#include "ui.h"

static uint64_t g_fps_window_start;
static int g_fps_frames;
static int g_fps_value;

void overlay_init(void) {
    ui_init();
}

void overlay_set_hint(const char *text) {
    (void)text;
}

void overlay_draw(int width, int height) {
    uint64_t now = monotonic_ms();
    g_fps_frames++;
    if (!g_fps_window_start) {
        g_fps_window_start = now;
    } else if (now - g_fps_window_start >= 500) {
        g_fps_value = (int)(g_fps_frames * 1000 / (now - g_fps_window_start));
        g_fps_frames = 0;
        g_fps_window_start = now;
        static uint64_t last_log;
        if (g_log_verbose && now - last_log >= 5000) {
            last_log = now;
            log_info("fps %d", g_fps_value);
        }
    }

    {
        static uint64_t perf_start, perf_frames;
        static int perf_on = -1;
        if (perf_on < 0) {
            perf_on = getenv("CODBOZ_PERF") != NULL;
        }
        perf_frames++;
        if (perf_on && !perf_start) {
            perf_start = monotonic_us();
            memset(&g_perf, 0, sizeof(g_perf));
            perf_frames = 0;
        } else if (perf_on && monotonic_us() - perf_start >= 1000000) {
            double total = (double)(monotonic_us() - perf_start), f = perf_frames ? (double)perf_frames : 1;
            double host = (double)g_perf.host_us - g_perf.wait_us;
            log_info("perf: %llu fps | per frame %.1f ms = emu %.1f + host %.1f (swap %.1f) + yield %.1f | %llu calls/frame",
                     (unsigned long long)perf_frames, total / f / 1000, (total - g_perf.host_us) / f / 1000,
                     (host - g_perf.swap_us) / f / 1000, g_perf.swap_us / f / 1000, g_perf.wait_us / f / 1000,
                     (unsigned long long)(g_perf.calls / perf_frames));
            {
                extern void emu_perf_top(char *out, int size);
                char top[256];
                emu_perf_top(top, sizeof(top));
                log_info("perf: top calls/s %s", top);
            }
            perf_start = monotonic_us();
            memset(&g_perf, 0, sizeof(g_perf));
            perf_frames = 0;
        }
    }
    extern uint32_t video_game_fbo(void);
    screens_capture(video_game_fbo(), video_width(), video_height());
    reimagined_frame();
    menus_frame();

    ui_begin(width, height);
    menus_draw();
    reimagined_draw();
    if (g_opt.show_fps) {
        char text[32];
        snprintf(text, sizeof(text), "FPS %d", g_fps_value);
        ui_text(text, 640, 712, 22, 0xfff5c518u, UI_CENTER);
    }
    ui_end();
}
