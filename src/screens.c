/*
 * Screen recognition, ported from boz-reimagined's Menus.java: the frame is reduced
 * to a 160x90 grey grid and compared with the probes in boz_menus.json (taken from
 * TV screenshots). It tells the host which menu is up, so the cursor, the pause timer
 * flag and the reimagined overlays follow the game by themselves.
 */
#include "screens.h"
#include "host.h"
#include "reimagined.h"

#include "../third_party/cJSON.h"

#include <float.h>
#include <stdlib.h>

enum {
    MAX_SCREENS = 64,
    INTERVAL_MS = 120,
    CAP_W = 320,
    CAP_H = 180,
    SCENE_MIN_DRAWS = 20, /* per INTERVAL_MS */
};

#define THRESHOLD 16.0f
#define THRESHOLD2 26.0f

struct screen {
    char id[32];
    struct screen_item items[24];
    int item_count;
    int px, py, pw, ph;
    int *ref;
    int has2;
    int p2[4];
    int *ref2;
    char after[6][32];
    int after_count;
};

static struct screen g_screens[MAX_SCREENS];
static int g_count, g_cell = 8, g_shift = 2, g_gw = 160, g_gh = 90;
static const struct screen *g_current, *g_last_known;
static bool g_coop_session;

bool screens_coop_session(void) {
    /* In a match the game's own flag decides: the frame recognition can miss the co-op lobby. */
    if (getenv("CODBOZ_PREVIEW_COOP")) { /* debug: see the co-op menus in a solo match */
        return true;
    }
    extern int reimagined_match_coop(void);
    int native = reimagined_match_coop();
    return native >= 0 ? native != 0 : g_coop_session;
}
static uint64_t g_next_ms, g_changed_ms, g_unknown_since;
static uint32_t g_fbo, g_rb;
static bool g_scene;
static uint64_t g_scene_changed_ms;
static uint8_t g_pixels[CAP_W * CAP_H * 4];

static struct {
    void (*GenFramebuffers)(int, uint32_t *);
    void (*BindFramebuffer)(uint32_t, uint32_t);
    void (*GenRenderbuffers)(int, uint32_t *);
    void (*BindRenderbuffer)(uint32_t, uint32_t);
    void (*RenderbufferStorage)(uint32_t, uint32_t, int, int);
    void (*FramebufferRenderbuffer)(uint32_t, uint32_t, uint32_t, uint32_t);
    void (*BlitFramebuffer)(int, int, int, int, int, int, int, int, uint32_t, uint32_t);
    void (*ReadPixels)(int, int, int, int, uint32_t, uint32_t, void *);
    void (*GetIntegerv)(uint32_t, int32_t *);
    void (*PixelStorei)(uint32_t, int);
    void (*Disable)(uint32_t);
    void (*Enable)(uint32_t);
    uint8_t (*IsEnabled)(uint32_t);
} gl;

static int *int_array(const cJSON *array, int *n) {
    *n = cJSON_GetArraySize(array);
    int *out = calloc((size_t)(*n ? *n : 1), sizeof(int));
    for (int i = 0; i < *n; ++i) {
        out[i] = cJSON_GetArrayItem(array, i)->valueint;
    }
    return out;
}

bool screens_load(const char *json_path) {
    FILE *f = fopen(json_path, "rb");
    if (!f) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)len + 1);
    size_t got = fread(text, 1, (size_t)len, f);
    fclose(f);
    text[got] = 0;
    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) {
        return false;
    }
    g_cell = cJSON_GetObjectItem(root, "cell")->valueint;
    const cJSON *shift = cJSON_GetObjectItem(root, "shift");
    g_shift = shift ? shift->valueint : 2;
    g_gw = cJSON_GetObjectItem(root, "w")->valueint / g_cell;
    g_gh = cJSON_GetObjectItem(root, "h")->valueint / g_cell;
    const cJSON *list = cJSON_GetObjectItem(root, "screens");
    for (int i = 0; i < cJSON_GetArraySize(list) && g_count < MAX_SCREENS; ++i) {
        const cJSON *o = cJSON_GetArrayItem(list, i);
        struct screen *s = &g_screens[g_count++];
        memset(s, 0, sizeof(*s));
        snprintf(s->id, sizeof(s->id), "%s", cJSON_GetObjectItem(o, "id")->valuestring);
        int n;
        int *p = int_array(cJSON_GetObjectItem(o, "probe"), &n);
        s->px = p[0];
        s->py = p[1];
        s->pw = p[2];
        s->ph = p[3];
        free(p);
        s->ref = int_array(cJSON_GetObjectItem(o, "ref"), &n);
        const cJSON *p2 = cJSON_GetObjectItem(o, "probe2");
        if (p2) {
            int *q = int_array(p2, &n);
            memcpy(s->p2, q, sizeof(s->p2));
            free(q);
            s->ref2 = int_array(cJSON_GetObjectItem(o, "ref2"), &n);
            s->has2 = 1;
        }
        const cJSON *items = cJSON_GetObjectItem(o, "items");
        for (int k = 0; items && k < cJSON_GetArraySize(items) && s->item_count < 24; ++k) {
            const cJSON *it = cJSON_GetArrayItem(items, k);
            struct screen_item *item = &s->items[s->item_count++];
            memset(item, 0, sizeof(*item));
            item->slider = strcmp(cJSON_GetArrayItem(it, 0)->valuestring, "s") == 0;
            float a = (float)cJSON_GetArrayItem(it, 1)->valuedouble;
            float b = (float)cJSON_GetArrayItem(it, 2)->valuedouble;
            float c = (float)cJSON_GetArrayItem(it, 3)->valuedouble;
            float d = (float)cJSON_GetArrayItem(it, 4)->valuedouble;
            if (item->slider) {
                item->x = (a < c ? a : c) - 20;
                item->y = (b < d ? b : d) - 20;
                item->w = (a > c ? a - c : c - a) + 40;
                item->h = (b > d ? b - d : d - b) + 40;
            } else {
                item->x = a;
                item->y = b;
                item->w = c;
                item->h = d;
            }
            item->tx = item->x + item->w / 2;
            item->ty = item->y + item->h / 2;
            for (int t = 5; t < cJSON_GetArraySize(it); ++t) {
                const char *tag = cJSON_GetArrayItem(it, t)->valuestring;
                if (strncmp(tag, "tap:", 4) == 0) {
                    sscanf(tag + 4, "%f,%f", &item->tx, &item->ty);
                } else if (strncmp(tag, "to:", 3) == 0) {
                    snprintf(item->to, sizeof(item->to), "%s", tag + 3);
                } else if (strcmp(tag, "stay") == 0) {
                    item->stay = true;
                } else if (strcmp(tag, "game") == 0) {
                    item->to_game = true;
                }
            }
        }
        const cJSON *after = cJSON_GetObjectItem(o, "after");
        for (int k = 0; after && k < cJSON_GetArraySize(after) && k < 6; ++k) {
            snprintf(s->after[k], sizeof(s->after[k]), "%s",
                     cJSON_GetArrayItem(after, k)->valuestring);
            s->after_count++;
        }
    }
    cJSON_Delete(root);
    log_info("screen recognition: %d screens", g_count);
    return g_count > 0;
}

static float match_probe(int px, int py, int pw, int ph, const int *ref, const int *g) {
    float best = FLT_MAX;
    for (int dy = -g_shift; dy <= g_shift; ++dy) {
        for (int dx = -g_shift; dx <= g_shift; ++dx) {
            int cx = px + dx, cy = py + dy;
            if (cx < 0 || cy < 0 || cx + pw > g_gw || cy + ph > g_gh) {
                continue;
            }
            long sum = 0;
            int k = 0;
            for (int j = 0; j < ph; ++j) {
                for (int i = 0; i < pw; ++i) {
                    sum += labs((long)g[(cy + j) * g_gw + cx + i] - ref[k++]);
                }
            }
            float d = (float)sum / (float)(pw * ph);
            if (d < best) {
                best = d;
            }
        }
    }
    return best;
}

static float match(const struct screen *s, const int *g) {
    float d = match_probe(s->px, s->py, s->pw, s->ph, s->ref, g);
    if (s->has2 && d < THRESHOLD) {
        float d2 = match_probe(s->p2[0], s->p2[1], s->p2[2], s->p2[3], s->ref2, g);
        d = d2 >= THRESHOLD2 ? FLT_MAX : (d + d2) / 2;
    }
    return d;
}

/* Screens that only exist outside a match. During a match a frame of gameplay (Dead Ops
 * Arcade's top-down view) can resemble one of them; recognising it would release the mouse
 * and turn Esc into that page's BACK instead of pausing. */
static bool front_end_only(const char *id) {
    static const char *const ids[] = {"title", "title_prompt", "main", "single", "newgame", "charselect",
                                      "coop", "coopwifi", "coopmap", "cooplobby", "coopvote", "reconnectconfirm",
                                      "player_profile", "extras"};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        if (strcmp(id, ids[i]) == 0) {
            return true;
        }
    }
    return false;
}

static int allowed_after(const struct screen *s) {
    if (reimagined_match_active() && front_end_only(s->id)) {
        return 0;
    }
    if (strcmp(s->id, "leavecoop") == 0 && !screens_coop_session()) {
        return 0;
    }
    /* Its burst art also resembles the native re-join dialog, where "this will erase the session"
     * would be a lie: the warning only follows Local Wi-Fi's page. */
    if (strcmp(s->id, "reconnectconfirm") == 0 && s != g_current) {
        const struct screen *from = g_current ? g_current : g_last_known;
        /* from == s: recognition missed a frame of this same dialog, it still counts */
        return from && (from == s || strcmp(from->id, "coopwifi") == 0);
    }
    if (!s->after_count || s == g_current) {
        return 1;
    }
    const struct screen *context = g_current ? g_current : g_last_known;
    for (int i = 0; context && i < s->after_count; ++i) {
        if (strcmp(s->after[i], context->id) == 0) {
            return 1;
        }
    }
    return 0;
}

static void recognize(void) {
    /* 320x180 colour -> 160x90 grey (2x2 average), then a 3x3 blur, as in Menus.java */
    static int grey[160 * 90], blurred[160 * 90];
    if (g_gw != 160 || g_gh != 90) {
        return;
    }
    for (int y = 0; y < 90; ++y) {
        for (int x = 0; x < 160; ++x) {
            int acc = 0;
            for (int k = 0; k < 4; ++k) {
                /* GL rows are bottom-up */
                int sy = CAP_H - 1 - (y * 2 + (k >> 1));
                const uint8_t *c = &g_pixels[(sy * CAP_W + x * 2 + (k & 1)) * 4];
                acc += (299 * c[0] + 587 * c[1] + 114 * c[2]) / 1000;
            }
            grey[y * 160 + x] = acc / 4;
        }
    }
    for (int y = 0; y < 90; ++y) {
        for (int x = 0; x < 160; ++x) {
            int acc = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                int yy = y + dy < 0 ? 0 : y + dy > 89 ? 89 : y + dy;
                for (int dx = -1; dx <= 1; ++dx) {
                    int xx = x + dx < 0 ? 0 : x + dx > 159 ? 159 : x + dx;
                    acc += grey[yy * 160 + xx];
                }
            }
            blurred[y * 160 + x] = acc / 9;
        }
    }
    const struct screen *best = NULL;
    float best_d = THRESHOLD;
    for (int i = 0; i < g_count; ++i) {
        if (!allowed_after(&g_screens[i])) {
            continue;
        }
        float d = match(&g_screens[i], blurred);
        if (d < best_d) {
            best_d = d;
            best = &g_screens[i];
        }
    }
    uint64_t now = monotonic_ms();
    /* Flash animates across recognition probes. Keep the last page briefly so
     * one missed sample cannot reveal or activate its underlying controls. */
    if (!best && g_current) {
        if (!g_unknown_since) {
            g_unknown_since = now;
        }
        uint64_t grace = strcmp(g_current->id, "charselect") == 0 ? 750 : 360;
        if (now - g_unknown_since < grace) {
            return;
        }
    } else {
        g_unknown_since = 0;
    }
    if (best != g_current) {
        if (best && strncmp(best->id, "coop", 4) == 0) {
            g_coop_session = true;
        } else if (best && (strcmp(best->id, "main") == 0 ||
                            strcmp(best->id, "single") == 0)) {
            /* only the solo path clears it: the co-op character pick can be recognised as
             * "charselect", and the game's network object keeps the last co-op party */
            g_coop_session = false;
        }
        if (g_current) {
            g_last_known = g_current;
        }
        g_current = best;
        g_changed_ms = now;
        log_debug("screen: %s", best ? best->id : "(game)");
    }
}

void screens_capture(uint32_t source_fbo, int width, int height) {
    if (!g_count) {
        return;
    }
    uint64_t now = monotonic_ms();
    if (now < g_next_ms) {
        return;
    }
    g_next_ms = now + INTERVAL_MS;
    /* 3D scene or flat menu: only a scene draws with the depth test on. */
    {
        extern unsigned gl_depth_draws;
        bool scene = gl_depth_draws >= SCENE_MIN_DRAWS;
        gl_depth_draws = 0;
        if (scene != g_scene) {
            g_scene = scene;
            g_scene_changed_ms = now;
            log_debug("scene: %s", scene ? "3d" : "flat");
        }
    }
    if (!g_fbo) {
        *(void **)&gl.GenFramebuffers = gl_proc("glGenFramebuffers");
        *(void **)&gl.BindFramebuffer = gl_proc("glBindFramebuffer");
        *(void **)&gl.GenRenderbuffers = gl_proc("glGenRenderbuffers");
        *(void **)&gl.BindRenderbuffer = gl_proc("glBindRenderbuffer");
        *(void **)&gl.RenderbufferStorage = gl_proc("glRenderbufferStorage");
        *(void **)&gl.FramebufferRenderbuffer = gl_proc("glFramebufferRenderbuffer");
        *(void **)&gl.BlitFramebuffer = gl_proc("glBlitFramebuffer");
        *(void **)&gl.ReadPixels = gl_proc("glReadPixels");
        *(void **)&gl.GetIntegerv = gl_proc("glGetIntegerv");
        *(void **)&gl.PixelStorei = gl_proc("glPixelStorei");
        *(void **)&gl.Disable = gl_proc("glDisable");
        *(void **)&gl.Enable = gl_proc("glEnable");
        *(void **)&gl.IsEnabled = gl_proc("glIsEnabled");
        int32_t rb_prev = 0;
        gl.GetIntegerv(0x8ca7, &rb_prev); /* GL_RENDERBUFFER_BINDING */
        gl.GenFramebuffers(1, &g_fbo);
        gl.GenRenderbuffers(1, &g_rb);
        gl.BindRenderbuffer(0x8d41, g_rb);
        gl.RenderbufferStorage(0x8d41, 0x8058, CAP_W, CAP_H); /* GL_RGBA8 */
        gl.BindRenderbuffer(0x8d41, (uint32_t)rb_prev);
    }
    int32_t read_fb = 0, draw_fb = 0, pack = 4;
    gl.GetIntegerv(0x8caa, &read_fb); /* GL_READ_FRAMEBUFFER_BINDING */
    gl.GetIntegerv(0x8ca6, &draw_fb); /* GL_DRAW_FRAMEBUFFER_BINDING */
    gl.GetIntegerv(0x0d05, &pack);
    uint8_t scissor = gl.IsEnabled(0x0c11);
    gl.Disable(0x0c11);
    gl.BindFramebuffer(0x8ca9, g_fbo); /* DRAW */
    gl.FramebufferRenderbuffer(0x8ca9, 0x8ce0, 0x8d41, g_rb);
    gl.BindFramebuffer(0x8ca8, source_fbo); /* READ: the game frame */
    gl.BlitFramebuffer(0, 0, width, height, 0, 0, CAP_W, CAP_H, 0x4000, 0x2601);
    gl.BindFramebuffer(0x8ca8, g_fbo);
    gl.PixelStorei(0x0d05, 1);
    gl.ReadPixels(0, 0, CAP_W, CAP_H, 0x1908, 0x1401, g_pixels);
    gl.PixelStorei(0x0d05, pack);
    gl.BindFramebuffer(0x8ca8, (uint32_t)read_fb);
    gl.BindFramebuffer(0x8ca9, (uint32_t)draw_fb);
    if (scissor) {
        gl.Enable(0x0c11);
    }
    recognize();
}

bool screens_scene_stable(void) {
    return g_scene && monotonic_ms() - g_scene_changed_ms > 1000;
}

const char *screens_current(void) {
    return g_current ? g_current->id : NULL;
}

const char *screens_last_known(void) {
    return g_last_known ? g_last_known->id : NULL;
}

uint64_t screens_changed_ms(void) {
    return g_changed_ms;
}

bool screens_available(void) {
    return g_count > 0;
}

bool screens_is(const char *id) {
    return g_current && strcmp(g_current->id, id) == 0;
}

const struct screen_item *screens_items(const char *id, int *count) {
    for (int i = 0; id && i < g_count; ++i) {
        if (strcmp(g_screens[i].id, id) == 0) {
            *count = g_screens[i].item_count;
            return g_screens[i].items;
        }
    }
    *count = 0;
    return NULL;
}

/* Grey 8x6 signature of the map art (x 450..840, y 60..390 in 1280x720), as Menus.java. */
void screens_art_signature(int out[48]) {
    float scale = (float)CAP_W / 1280.0f;
    for (int gy = 0; gy < 6; ++gy) {
        for (int gx = 0; gx < 8; ++gx) {
            int x0 = (int)((450 + gx * 48.75f) * scale + 0.5f), x1 = (int)((450 + (gx + 1) * 48.75f) * scale + 0.5f);
            int y0 = (int)((60 + gy * 55.0f) * scale + 0.5f), y1 = (int)((60 + (gy + 1) * 55.0f) * scale + 0.5f);
            int sum = 0, n = 0;
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const uint8_t *c = &g_pixels[((CAP_H - 1 - y) * CAP_W + x) * 4];
                    sum += (c[0] * 299 + c[1] * 587 + c[2] * 114) / 1000;
                    n++;
                }
            }
            out[gy * 8 + gx] = n ? sum / n : 0;
        }
    }
}
