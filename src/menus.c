/*
 * boz-reimagined's menus, redrawn by the host over the Flash menus:
 *   BrandScreen     title art; MainMenu  comic page with tiles (main-menu/layout.json);
 *   StyledScreen    pages and title tags (menus-style/screens.json);
 *   CharacterMenu   character and map selection, driving the game's own carousel;
 *   Cursor          focus brackets, transitions (curtain) and taps on the Flash menu.
 * A press on a drawn button touches the Flash menu where the TV build touches it.
 */
#include "menus.h"
#include "host.h"
#include "reimagined.h"
#include "screens.h"
#include "ui.h"

#include "menus_layout.h"

#include "../third_party/cJSON.h"

#include <SDL.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>

#define PAPER 0xfff2eee4u
#define INK 0xff151211u
#define RED 0xff791d24u
#define DIM 0xde191614u
#define SAND 0xffd5c9acu
#define EDGE 0xff827763u

enum { MAX_BUTTONS = 8, MAX_STYLES = 24, MAX_TARGETS = 32 };

enum target_kind {
    T_TAP,
    T_NAME,
    T_TIMER,
    T_CHAR,
    T_MAP,
    T_START,
    T_FPS,
    T_LOW_FPS,
};

struct button {
    char id[16], title[40], caption[48], to[24];
    float x, y, w, h, tx, ty;
    bool primary, has_tap, to_game, selection, timer;
    struct ui_image *art;
};

struct style {
    char id[24], title[32], back[24];
    bool page;
    char message[4][64];
    int message_count;
    float tag[4], window[4];
    bool has_tag, has_window;
    char hint[64];
    struct button buttons[MAX_BUTTONS];
    int button_count;
};

struct target {
    float x, y, w, h, tx, ty;
    enum target_kind kind;
    int index;
    char to[24];
    bool to_game;
};

static bool g_enabled;
static struct ui_image g_paper, g_title, g_strip, g_back, g_start, g_tick;
static struct ui_image g_portraits[4], g_scenes[4], g_maps[4];
static struct ui_image g_art_pool[8];
static char g_art_names[8][64];
static int g_art_count;
static char g_assets[1024];

static struct button g_main[8];
static int g_main_count;
static struct style g_styles[MAX_STYLES];
static int g_style_count;

static struct target g_targets[MAX_TARGETS];
static int g_target_count, g_focus = -1;
static int g_hover = -1; /* item under the mouse */
static bool g_pad_mode;
static const char *g_focus_screen;

/* curtain between screens */
static bool g_curtain;
static char g_curtain_from[24];
static uint64_t g_curtain_since;
static const char *g_prev_screen;
static const char *g_drawn_screen;
static const char *g_pending_screen;
static int g_pending_target = -1;
static bool g_screen_unknown;
static void activate(int index);

/* Recognition can miss a Flash animation. Only a different recognised page (or
 * entry into gameplay) ends the previous page's input/selection transaction. */
static bool note_screen(const char *screen, bool match_active) {
    bool changed = screen && screen != g_prev_screen;
    if (changed && g_prev_screen && strncmp(screen, "title", 5) == 0 &&
        strncmp(g_prev_screen, "title", 5) == 0) {
        changed = false; /* The blinking title prompt is still the same page. */
    }
    if (screen || match_active) g_prev_screen = screen;
    return changed;
}

/* character / map selection */
static const char *const k_map_names[4] = {"KINO DER TOTEN", "DEAD OPS ARCADE", "ASCENSION",
                                           "CALL OF THE DEAD"};
static int g_picked_map = 1, g_picked_char, g_carousel_at = -1;
static bool g_map_busy, g_map_failed, g_launching;
static bool g_start_requested;
static int g_start_retries;
static int g_home_misses;
static uint64_t g_map_deadline, g_arrow_tap_at, g_next_step, g_apply_char_at, g_start_at,
    g_launch_until;
static int g_arrow_dir, g_art_before[48];

/* name editor */
static bool g_name_open;
static char g_name[16];

void input_tap(float x, float y);
bool input_has_controller(void);

/* Footer hints for the device in use. */
static const char *hint_select_back(void) {
    return input_has_controller() ? "D-PAD: NAVIGATE     A: SELECT     B: BACK"
                                  : "CLICK: SELECT     ESC: BACK";
}
bool input_tap_busy(void);
void input_cancel_tap(void);

static struct ui_image *art(const char *name) {
    for (int i = 0; i < g_art_count; ++i) {
        if (strcmp(g_art_names[i], name) == 0) {
            return &g_art_pool[i];
        }
    }
    if (g_art_count >= 8) {
        return NULL;
    }
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", g_assets, name);
    snprintf(g_art_names[g_art_count], sizeof(g_art_names[0]), "%s", name);
    ui_image_load(&g_art_pool[g_art_count], path);
    return &g_art_pool[g_art_count++];
}

static void load_image(struct ui_image *im, const char *name) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", g_assets, name);
    ui_image_load(im, path);
}

static cJSON *load_json(const char *name) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", g_assets, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)len + 1);
    text[fread(text, 1, (size_t)len, f)] = 0;
    fclose(f);
    cJSON *root = cJSON_Parse(text);
    free(text);
    return root;
}

static const char *str(const cJSON *o, const char *key, const char *fallback) {
    const cJSON *v = cJSON_GetObjectItem(o, key);
    return cJSON_IsString(v) ? v->valuestring : fallback;
}

static void rect4(const cJSON *a, float out[4]) {
    for (int i = 0; i < 4; ++i) {
        out[i] = (float)cJSON_GetArrayItem(a, i)->valuedouble;
    }
}

static void parse_button(const cJSON *o, struct button *b) {
    memset(b, 0, sizeof(*b));
    snprintf(b->id, sizeof(b->id), "%s", str(o, "id", ""));
    snprintf(b->title, sizeof(b->title), "%s", str(o, "title", ""));
    snprintf(b->caption, sizeof(b->caption), "%s", str(o, "caption", ""));
    snprintf(b->to, sizeof(b->to), "%s", str(o, "to", ""));
    float r[4];
    rect4(cJSON_GetObjectItem(o, "bounds"), r);
    b->x = r[0];
    b->y = r[1];
    b->w = r[2];
    b->h = r[3];
    const cJSON *tap = cJSON_GetObjectItem(o, "tap");
    if (tap) {
        b->tx = (float)cJSON_GetArrayItem(tap, 0)->valuedouble;
        b->ty = (float)cJSON_GetArrayItem(tap, 1)->valuedouble;
        b->has_tap = true;
    }
    b->primary = cJSON_IsTrue(cJSON_GetObjectItem(o, "primary"));
    const char *action = str(o, "action", "");
    b->to_game = strcmp(action, "game") == 0;
    b->selection = strcmp(action, "selection") == 0;
    b->timer = strcmp(action, "timer") == 0;
    const char *a = str(o, "art", NULL);
    b->art = a ? art(a) : NULL;
}

static void set_button(struct button *b, const char *id, const char *title, const char *caption, float x,
                       float y, float w, float h, float tx, float ty, const char *to) {
    memset(b, 0, sizeof(*b));
    snprintf(b->id, sizeof(b->id), "%s", id);
    snprintf(b->title, sizeof(b->title), "%s", title);
    snprintf(b->caption, sizeof(b->caption), "%s", caption);
    snprintf(b->to, sizeof(b->to), "%s", to);
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    b->tx = tx;
    b->ty = ty;
    b->has_tap = true;
}

/* Co-op's first two pages. Play Online is gone: its servers are off. Tap points are the native ones. */
static void define_coop_pages(void) {
    for (int i = 0; i < g_style_count; ++i) {
        struct style *s = &g_styles[i];
        if (strcmp(s->id, "coop") == 0) {
            s->page = true;
            s->has_tag = false;
            snprintf(s->title, sizeof(s->title), "CO-OP MULTIPLAYER");
            s->button_count = 3;
            set_button(&s->buttons[0], "wifi", "LOCAL WI-FI", "HOST OR JOIN ON YOUR NETWORK", 96, 170, 650, 380, 620, 360, "coopwifi");
            set_button(&s->buttons[1], "rejoin", "RE-JOIN", "LAST DISCONNECTED SESSION", 770, 170, 410, 380, 880, 270, "*");
            s->buttons[0].art = art("main-menu/coop.png");
            s->buttons[1].art = art("main-menu/profile.png");
            set_button(&s->buttons[2], "back", "BACK", "", 76, 560, 240, 104, 267, 622, "main");
        } else if (strcmp(s->id, "gameover") == 0) {
            /* the native score card starts left of the measured window */
            s->window[0] = 120;
            s->window[1] = 140;
            s->window[2] = 700;
            s->window[3] = 390;
        } else if (strcmp(s->id, "pause") == 0) {
            /* pause_solo.psd: a wider opening for the native card */
            s->window[0] = 196;
            s->window[1] = 272;
            s->window[2] = 646;
            s->window[3] = 228;
        } else if (strcmp(s->id, "coopwifi") == 0) {
            s->page = true;
            s->has_tag = false;
            snprintf(s->title, sizeof(s->title), "LOCAL WI-FI");
            s->button_count = 3;
            set_button(&s->buttons[0], "single-map", "SINGLE MAP", "CHOOSE THE MAP FOR EVERYONE", 96, 170, 530, 380, 360, 320, "coopmap");
            set_button(&s->buttons[1], "map-vote", "MAP VOTE", "LET THE PARTY DECIDE", 650, 170, 530, 380, 850, 400, "*");
            set_button(&s->buttons[2], "back", "BACK", "", 76, 560, 240, 104, 265, 620, "coop");
        }
    }
}

void menus_init(const char *assets_dir) {
    uint64_t started = monotonic_ms();
    snprintf(g_assets, sizeof(g_assets), "%s", assets_dir);
    cJSON *layout = load_json("main-menu/layout.json");
    cJSON *screens = load_json("menus-style/screens.json");
    if (!layout || !screens) {
        cJSON_Delete(layout);
        cJSON_Delete(screens);
        return;
    }
    const cJSON *items = cJSON_GetObjectItem(layout, "items");
    for (int i = 0; i < cJSON_GetArraySize(items) && g_main_count < 8; ++i) {
        const cJSON *o = cJSON_GetArrayItem(items, i);
        struct button *b = &g_main[g_main_count++];
        parse_button(o, b);
        if (strcmp(str(o, "action", ""), "name") == 0) {
            snprintf(b->to, sizeof(b->to), "name");
        }
    }
    const cJSON *list = cJSON_GetObjectItem(screens, "screens");
    for (const cJSON *o = list ? list->child : NULL; o && g_style_count < MAX_STYLES; o = o->next) {
        struct style *s = &g_styles[g_style_count++];
        memset(s, 0, sizeof(*s));
        snprintf(s->id, sizeof(s->id), "%s", o->string);
        snprintf(s->title, sizeof(s->title), "%s", str(o, "title", ""));
        snprintf(s->back, sizeof(s->back), "%s", str(o, "back", ""));
        s->page = strcmp(str(o, "kind", ""), "page") == 0;
        snprintf(s->hint, sizeof(s->hint), "%s", str(o, "hint", ""));
        const cJSON *m = cJSON_GetObjectItem(o, "message");
        for (int i = 0; m && i < cJSON_GetArraySize(m) && i < 4; ++i) {
            snprintf(s->message[i], sizeof(s->message[i]), "%s", cJSON_GetArrayItem(m, i)->valuestring);
            s->message_count++;
        }
        if (cJSON_GetObjectItem(o, "tag")) {
            rect4(cJSON_GetObjectItem(o, "tag"), s->tag);
            s->has_tag = true;
        }
        const cJSON *w = cJSON_GetObjectItem(o, "windows");
        if (w && cJSON_GetArraySize(w) > 0) {
            rect4(cJSON_GetArrayItem(w, 0), s->window);
            s->has_window = true;
        }
        const cJSON *b = cJSON_GetObjectItem(o, "buttons");
        for (int i = 0; b && i < cJSON_GetArraySize(b) && s->button_count < MAX_BUTTONS; ++i) {
            parse_button(cJSON_GetArrayItem(b, i), &s->buttons[s->button_count++]);
        }
    }
    cJSON_Delete(layout);
    cJSON_Delete(screens);
    define_coop_pages();

    load_image(&g_paper, "character-menu/paper.png");
    load_image(&g_title, "branding/title-reimagined-v1.png");
    load_image(&g_strip, "character-menu/title.png");
    load_image(&g_back, "character-menu/back.png");
    load_image(&g_start, "character-menu/start.png");
    load_image(&g_tick, "character-menu/tick.png");
    static const char *portraits[4] = {"nikolai", "takeo", "richtofen", "dempsey"};
    static const char *scenes[4] = {"background", "arcade-background", "ascension-background",
                                    "cotd-background"};
    char name[96];
    for (int i = 0; i < 4; ++i) {
        snprintf(name, sizeof(name), "character-menu/%s.png", portraits[i]);
        load_image(&g_portraits[i], name);
        snprintf(name, sizeof(name), "character-menu/%s.png", scenes[i]);
        load_image(&g_scenes[i], name);
        static const char *maps[4] = {"kino", "deadops", "ascension", "cotd"};
        snprintf(name, sizeof(name), "character-menu/%s.png", maps[i]);
        load_image(&g_maps[i], name);
    }
    g_enabled = g_paper.tex != 0;
    log_info("reimagined menus: %s (%d styled screens) in %llu ms", g_enabled ? "on" : "off", g_style_count,
             (unsigned long long)(monotonic_ms() - started));
}

bool menus_enabled(void) {
    return g_enabled && reimagined_patched();
}

void menus_set_pad_mode(bool pad) {
    g_pad_mode = pad;
}

/* ---- drawing helpers ---- */

static void text(const char *s, float x, float baseline, float size, uint32_t color,
                 float max_width, bool outline, enum ui_align align) {
    ui_font(UI_FONT_DISPLAY);
    float w = ui_text_width(s, size);
    if (w > max_width && w > 0) {
        size = size * max_width / w;
    }
    if (outline) {
        ui_text(s, x, baseline, size, color, align);
    } else {
        ui_label(s, x, baseline, size, color, align);
    }
}

/* Text placed by hand in the PSDs: k_layout holds its offset, scale and tilt for this screen. */
static const char *g_layout_screen = "";

static void placed_text(const char *layer, const char *s, float x, float baseline, float size, uint32_t color,
                        float max_width, bool outline, enum ui_align align) {
    const struct layout_adj *adj = NULL;
    for (size_t i = 0; i < sizeof(k_layout) / sizeof(k_layout[0]); ++i) {
        if (strcmp(k_layout[i].screen, g_layout_screen) == 0 && strcmp(k_layout[i].layer, layer) == 0) {
            adj = &k_layout[i];
            break;
        }
    }
    if (!adj) {
        text(s, x, baseline, size, color, max_width, outline, align);
        return;
    }
    ui_font(UI_FONT_DISPLAY);
    float w = ui_text_width(s, size);
    if (w > max_width && w > 0) {
        size *= max_width / w;
        w = max_width;
    }
    float left = align == UI_CENTER ? x - w / 2 : align == UI_RIGHT ? x - w : x;
    float mid = left + w / 2 + adj->dx, base = baseline + adj->dy;
    /* scaling keeps the same centre */
    float scaled = size * adj->scale;
    base += (scaled - size) * 0.35f;
    ui_rotate_begin(mid, base - scaled * 0.35f, adj->deg);
    text(s, mid, base, scaled, color, 1e6f, outline, UI_CENTER);
    ui_rotate_end();
}

static void comic_pts(float x, float y, float w, float h, float skew, float out[8]) {
    float pts[8] = {x, y + skew, x + w, y, x + w - 3, y + h - skew, x + 2, y + h};
    memcpy(out, pts, sizeof(pts));
}

static void comic_outline(const float pts[8]) {
    ui_quad_outline(pts, 5, INK);
    ui_quad_outline(pts, 1.5f, EDGE);
}

/*
 * The menu kit: every drawn screen is built from these, taken from the game's own menus. A torn
 * paper sheet on black, an ink body, a white title strip, comic panels with outlined white
 * captions, a red BACK arrow, a green go arrow and the orange burst of its dialogs.
 */
enum { SHEET_X0 = 44, SHEET_Y0 = 14, SHEET_X1 = 1236, SHEET_Y1 = 690, BODY_X0 = 72, BODY_Y0 = 62, BODY_X1 = 1208, BODY_Y1 = 668 };

static float torn(int i) { /* stable jitter: the same tear every frame */
    uint32_t h = (uint32_t)i * 2654435761u;
    return (float)((h >> 24) % 9);
}

/* Black teeth along one edge, biting into the paper. */
static void torn_edge(float x0, float y0, float x1, float y1, float nx, float ny, int seed) {
    float len = hypotf(x1 - x0, y1 - y0);
    int count = (int)(len / 38);
    for (int i = 0; i < count; ++i) {
        float t0 = (float)i / (float)count, t1 = (float)(i + 1) / (float)count, tm = (t0 + t1) / 2;
        float depth = 2 + torn(seed + i);
        ui_triangle(x0 + (x1 - x0) * t0, y0 + (y1 - y0) * t0, x0 + (x1 - x0) * t1, y0 + (y1 - y0) * t1,
                    x0 + (x1 - x0) * tm + nx * depth, y0 + (y1 - y0) * tm + ny * depth, 0xff070606u);
    }
}

/* The halftone sunburst behind the game's panels, kept inside the body. */
static void kit_rays(float cx, float cy, uint32_t color) {
    for (int i = 0; i < 28; ++i) {
        float a0 = (float)i * 0.2244f, a1 = a0 + 0.11f;
        float pts[8] = {cx, cy, cx, cy, 0, 0, 0, 0};
        float r = 760;
        float x0 = cx + r * cosf(a0), y0 = cy + r * sinf(a0), x1 = cx + r * cosf(a1), y1 = cy + r * sinf(a1);
        pts[4] = fminf(BODY_X1 - 4, fmaxf(BODY_X0 + 4, x1));
        pts[5] = fminf(BODY_Y1 - 4, fmaxf(BODY_Y0 + 6, y1));
        pts[6] = fminf(BODY_X1 - 4, fmaxf(BODY_X0 + 4, x0));
        pts[7] = fminf(BODY_Y1 - 4, fmaxf(BODY_Y0 + 6, y0));
        ui_triangle(pts[0], pts[1], pts[4], pts[5], pts[6], pts[7], color);
    }
}

/* Art cropped to the rectangle: it never spills over the sheet. */
static void kit_cover(const struct ui_image *im, float x, float y, float w, float h, uint32_t shade) {
    if (!im->tex) {
        return;
    }
    float aspect = w / h, iaspect = (float)im->w / (float)im->h;
    float u0 = 0, u1 = 1, v0 = 0, v1 = 1;
    if (iaspect > aspect) {
        u0 = (1 - aspect / iaspect) / 2;
        u1 = 1 - u0;
    } else {
        v0 = (1 - iaspect / aspect) * 0.4f;
        v1 = v0 + iaspect / aspect;
    }
    ui_image_part(im, x, y, w, h, u0, v0, u1, v1, 1.0f);
    ui_rect(x, y, w, h, shade);
}

static void kit_sheet(bool body) {
    /* no stencil here: a page's live window may be masking this very drawing */
    ui_image(&g_paper, 0, 0, 1280, 720, 1.0f);
    ui_rect(0, 0, 1280, SHEET_Y0, 0xff070606u);
    ui_rect(0, SHEET_Y1, 1280, 720 - SHEET_Y1, 0xff070606u);
    ui_rect(0, SHEET_Y0, SHEET_X0, SHEET_Y1 - SHEET_Y0, 0xff070606u);
    ui_rect(SHEET_X1, SHEET_Y0, 1280 - SHEET_X1, SHEET_Y1 - SHEET_Y0, 0xff070606u);
    torn_edge(SHEET_X0, SHEET_Y0, SHEET_X1, SHEET_Y0, 0, 1, 0);
    torn_edge(SHEET_X1, SHEET_Y0, SHEET_X1, SHEET_Y1, -1, 0, 40);
    torn_edge(SHEET_X0, SHEET_Y1, SHEET_X1, SHEET_Y1, 0, -1, 80);
    torn_edge(SHEET_X0, SHEET_Y0, SHEET_X0, SHEET_Y1, 1, 0, 120);
    if (body) {
        float pts[8] = {BODY_X0, BODY_Y0 + 4, BODY_X1, BODY_Y0, BODY_X1 - 4, BODY_Y1 - 4, BODY_X0 + 3, BODY_Y1};
        ui_quad(pts, 0xff0d0c0bu);
        kit_rays(640, 400, 0xff1d1211u);
    }
}

/* title.png's strip fills its top two thirds; the caption is ink, sized to fit inside it. */
static void kit_strip(float x, float y, float w, const char *title, const char *layer) {
    float h = w / 4;
    ui_image(&g_strip, x, y, w, h, 1.0f);
    float size = fminf(h * 0.42f, 44);
    placed_text(layer, title, x + w * 0.09f, y + h * 0.45f, size, INK, w * 0.80f, false, UI_LEFT);
}

static void draw_chrome(void) {
    kit_sheet(true);
}

static void kit_page_title(const char *title) {
    kit_strip(60, 8, 520, title, "title");
    text("BLACK OPS ZOMBIES", 1188, 112, 26, SAND, 560, true, UI_RIGHT);
    text("REIMAGINED", 1188, 152, 38, PAPER, 560, true, UI_RIGHT);
}

/* Footer on the black under the sheet. */
static void hint(const char *s, bool on_paper) {
    (void)on_paper;
    text(s, 640, 711, 18, SAND, 1000, false, UI_CENTER);
}

static void kit_arrow(bool back, float x, float y, float w, float h, const char *label, const char *id, bool dim) {
    const struct ui_image *im = back ? &g_back : &g_start;
    float iw = fminf(w, fmaxf(h * 2.2f, 240)), ih = iw / 2, ix = x + (w - iw) / 2, iy = y + h - ih + 8;
    ui_image(im, ix, iy, iw, ih, dim ? 0.55f : 1.0f);
    float size = fminf(ih * 0.36f, 38);
    char layer[32];
    snprintf(layer, sizeof(layer), "%s label", id);
    placed_text(layer, label, ix + iw * (back ? 0.56f : 0.46f), iy + ih * 0.48f, size, dim ? 0xffbcb8adu : PAPER, iw * 0.72f, true, UI_CENTER);
}

static float g_panel_art_side, g_panel_art_top; /* set from a PSD for one panel */

/* A comic panel: art (or plain ink), an outlined caption inside, a thin paper edge. */
static void kit_panel(float x, float y, float w, float h, const struct ui_image *im, const char *title,
                      const char *caption, bool selected, bool dim) {
    float pts[8];
    comic_pts(x, y, w, h, h > 100 ? 6 : 4, pts);
    ui_mask_begin();
    ui_quad(pts, 0xffffffffu);
    ui_mask_use(true);
    ui_rect(x, y, w, h, 0xff151211u);
    if (im && im->tex) {
        if (im->w == im->h) { /* round map art: whole, centred on the panel, never cropped */
            float side = g_panel_art_side > 0 ? g_panel_art_side : fmaxf(w, h) * 0.86f;
            float top = g_panel_art_side > 0 ? g_panel_art_top : y + (h - side) * 0.35f;
            ui_image(im, x + (w - side) / 2, top, side, side, 1.0f);
        } else {
            ui_image_cover(im, x, y, w, h, 1.0f);
        }
        if (dim) {
            ui_rect(x, y, w, h, 0x58000000u);
        }
        ui_rect(x, y + h * 0.62f, w, h * 0.38f, 0x70000000u);
    }
    ui_mask_end();
    bool small = h <= 100;
    float size = small ? fminf(30, h * 0.44f) : fminf(40, w * 0.16f);
    float base = small ? y + h / 2 + size * 0.36f : y + h - (caption && caption[0] ? 48 : 22);
    text(title, x + 18, base, size, selected ? 0xfff5c518u : PAPER, w - 36, true, UI_LEFT);
    if (caption && caption[0]) {
        text(caption, x + 18, y + h - 18, 20, SAND, w - 36, true, UI_LEFT);
    }
    ui_quad_outline(pts, 5, INK);
    ui_quad_outline(pts, 1.5f, selected ? RED : EDGE);
}

/* The dialogs' burst: rays over black, for the messages that interrupt a page. */
static void kit_burst(void) {
    ui_rect(0, 0, 1280, 720, 0xff0a0807u);
    const float cx = 640, cy = 360, r = 900;
    for (int i = 0; i < 16; ++i) {
        float a0 = (float)i * 0.3927f + 0.08f, a1 = a0 + 0.2f + 0.05f * (float)(i % 3);
        ui_triangle(cx, cy, cx + r * cosf(a0), cy + r * sinf(a0), cx + r * cosf(a1), cy + r * sinf(a1),
                    i % 2 ? 0xff6b3a1cu : 0xff8a4d25u);
    }
    ui_rect(0, 0, 1280, 720, 0x40000000u);
}

static const struct ui_image *tile_art(const struct button *b) {
    if (b->art) {
        return b->art;
    }
    if (strcmp(b->id, "new") == 0) {
        return &g_maps[0];
    }
    if (strcmp(b->id, "continue") == 0) {
        return &g_maps[3];
    }
    if (strcmp(b->id, "single-map") == 0) {
        return &g_maps[2];
    }
    if (strcmp(b->id, "map-vote") == 0) {
        return &g_maps[0];
    }
    return NULL;
}

static void draw_tile(const struct button *b, bool primary_red) {
    (void)primary_red;
    if (strcmp(b->id, "back") == 0 || strcmp(b->id, "no") == 0 || strcmp(b->id, "quit") == 0 ||
        strcmp(b->id, "return") == 0) {
        kit_arrow(true, b->x, b->y, b->w, b->h, b->title, b->id, false);
        return;
    }
    if (strcmp(b->id, "ok") == 0 || strcmp(b->id, "yes") == 0 || strcmp(b->id, "retry") == 0) {
        kit_arrow(false, b->x, b->y, b->w, b->h, b->title, b->id, false);
        return;
    }
    char title[64];
    if (b->timer) {
        extern bool reimagined_timer_shown(void);
        snprintf(title, sizeof(title), "TIMER: %s", reimagined_timer_shown() ? "ON" : "OFF");
    } else if (strcmp(b->to, "name") == 0) {
        snprintf(title, sizeof(title), "PLAYER: %s", host_player_name());
    } else {
        snprintf(title, sizeof(title), "%s", b->title);
    }
    extern bool reimagined_rejoin_available(void);
    bool off = strcmp(b->id, "rejoin") == 0 && !reimagined_rejoin_available();
    if (strcmp(b->id, "profile") == 0) { /* main.psd: the terminal fills the panel to its top */
        g_panel_art_side = 357;
        g_panel_art_top = 168;
    }
    kit_panel(b->x, b->y, b->w, b->h, tile_art(b), title, off ? "NO SESSION TO RE-JOIN" : b->caption, false, off);
    g_panel_art_side = 0;
}

/* ---- targets and focus ---- */

static void add_target(float x, float y, float w, float h, float tx, float ty, enum target_kind kind,
                       int index, const char *to, bool to_game) {
    if (g_target_count >= MAX_TARGETS) {
        return;
    }
    struct target *t = &g_targets[g_target_count++];
    *t = (struct target){x, y, w, h, tx, ty, kind, index, "", to_game};
    snprintf(t->to, sizeof(t->to), "%s", to ? to : "");
}

static const struct style *style_of(const char *id) {
    if (id && strcmp(id, "pause") == 0 && screens_coop_session()) {
        id = "pausecoop";
    }
    for (int i = 0; id && i < g_style_count; ++i) {
        if (strcmp(g_styles[i].id, id) == 0) {
            return &g_styles[i];
        }
    }
    return NULL;
}

static bool coop_native_pause(const char *id) {
    return id && screens_coop_session() && (strcmp(id, "pause") == 0 || strcmp(id, "pausecoop") == 0);
}

enum { FPS_PLATE_X = 610, FPS_PLATE_Y = 190, FPS_PLATE_W = 470, FPS_PLATE_H = 50 };

static const struct button *coop_timer_button(const struct style *s);
static void coop_timer_bounds(struct button *b);

static float k_chars_hit_y(int i);
static float k_chars_hit_h(int i);
static void map_panel_rect(int i, float *x, float *y, float *w, float *h);

static void add_buttons(const struct button *buttons, int count) {
    extern bool reimagined_rejoin_available(void);
    for (int i = 0; i < count; ++i) {
        const struct button *b = &buttons[i];
        if (strcmp(b->id, "rejoin") == 0 && !reimagined_rejoin_available()) {
            continue; /* nothing to re-join: neither the mouse nor the focus can land on it */
        }
        enum target_kind kind = strcmp(b->to, "name") == 0 ? T_NAME : b->timer ? T_TIMER : T_TAP;
        float tx = b->has_tap ? b->tx : b->x + b->w / 2, ty = b->has_tap ? b->ty : b->y + b->h / 2;
        add_target(b->x, b->y, b->w, b->h, tx, ty, kind, i, b->to, b->to_game);
    }
}

static void build_targets(const char *screen) {
    g_target_count = 0;
    if (!screen) {
        return;
    }
    const struct style *s = style_of(screen);
    if (strcmp(screen, "main") == 0) {
        add_buttons(g_main, g_main_count);
    } else if (strcmp(screen, "charselect") == 0) {
        for (int i = 0; i < 4; ++i) {
            add_target(86, k_chars_hit_y(i), 290, k_chars_hit_h(i), 0, 0, T_CHAR, i, "", false);
        }
        for (int i = 0; i < 4; ++i) {
            float x, y, w, h;
            map_panel_rect(i, &x, &y, &w, &h);
            add_target(x, y, w, h, 0, 0, T_MAP, i + 1, "", false);
        }
        add_target(960, 560, 240, 104, 0, 0, T_START, 0, "", false);
        add_target(76, 560, 240, 104, 267, 622, T_TAP, 0, "*", false);
    } else if (s && s->page && coop_native_pause(screen)) {
        /* the native controls, for the controller; the mouse reaches them directly */
        add_target(115, 593, 222, 97, 200, 633, T_TAP, 0, "", true);
        add_target(279, 166, 177, 181, 374, 290, T_TAP, 1, "pausesettings", false);
        add_target(579, 380, 183, 144, 671, 475, T_TAP, 2, "leavecoop", false);
        const struct button *timer = coop_timer_button(s);
        if (timer) {
            struct button b = *timer;
            coop_timer_bounds(&b);
            add_buttons(&b, 1);
        }
    } else if (s && s->page) {
        add_buttons(s->buttons, s->button_count);
    } else {
        if (strcmp(screen, "pausesettings") == 0) {
            add_target(FPS_PLATE_X, FPS_PLATE_Y, FPS_PLATE_W, FPS_PLATE_H, 0, 0, T_FPS, 0, "", false);
            add_target(FPS_PLATE_X, FPS_PLATE_Y + FPS_PLATE_H + 8, FPS_PLATE_W, FPS_PLATE_H, 0, 0, T_LOW_FPS, 0, "", false);
        }
        int count;
        const struct screen_item *items = screens_items(screen, &count);
        for (int i = 0; i < count; ++i) {
            if (!items[i].slider) {
                add_target(items[i].x, items[i].y, items[i].w, items[i].h, items[i].tx, items[i].ty,
                           T_TAP, i, items[i].to, items[i].to_game);
            }
        }
    }
}

static int target_at(float x, float y) {
    for (int i = g_target_count - 1; i >= 0; --i) {
        const struct target *t = &g_targets[i];
        if (x >= t->x && x < t->x + t->w && y >= t->y && y < t->y + t->h) {
            return i;
        }
    }
    return -1;
}

/* Corner brackets around the focused item, black under white. */
static void draw_focus(void) {
    if (g_focus < 0 || g_focus >= g_target_count) {
        return;
    }
    const struct target *t = &g_targets[g_focus];
    float pulse = 3.0f * sinf((float)(monotonic_ms() % 1000) / 1000.0f * 6.2831853f);
    float x0 = t->x - 6 - pulse, y0 = t->y - 6 - pulse, x1 = t->x + t->w + 6 + pulse,
          y1 = t->y + t->h + 6 + pulse, len = fminf(34, fminf(t->w, t->h) / 3);
    for (int pass = 0; pass < 2; ++pass) {
        float th = pass ? 4 : 8;
        uint32_t c = pass ? 0xffffffffu : INK;
        ui_rect(x0 - th / 2, y0 - th / 2, len, th, c);
        ui_rect(x0 - th / 2, y0 - th / 2, th, len, c);
        ui_rect(x1 - len + th / 2, y0 - th / 2, len, th, c);
        ui_rect(x1 - th / 2, y0 - th / 2, th, len, c);
        ui_rect(x0 - th / 2, y1 - th / 2, len, th, c);
        ui_rect(x0 - th / 2, y1 - len + th / 2, th, len, c);
        ui_rect(x1 - len + th / 2, y1 - th / 2, len, th, c);
        ui_rect(x1 - th / 2, y1 - len + th / 2, th, len, c);
    }
}

/* ---- curtain ---- */

static bool in_match_menu(const char *id) {
    static const char *const ids[] = {"pause", "pausesettings", "sensitivity", "savequit", "leavecoop",
                                      "quitconfirm", "gameover"};
    for (size_t i = 0; id && i < sizeof(ids) / sizeof(ids[0]); ++i) {
        if (strcmp(id, ids[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void begin_curtain(const char *from, float tx, float ty) {
    g_curtain = true;
    snprintf(g_curtain_from, sizeof(g_curtain_from), "%s", from ? from : "");
    g_curtain_since = monotonic_ms();
}

static void update_curtain(const char *screen) {
    if (!g_curtain) {
        return;
    }
    uint64_t t = monotonic_ms() - g_curtain_since;
    if (screen && strcmp(screen, g_curtain_from) != 0) {
        g_curtain = false;
    } else if (t >= 900 && screen) {
        /* Never replay a navigation touch: its destination may reuse that point. */
        g_curtain = false;
    } else if (t >= 2500) {
        g_curtain = false;
    }
}

static void draw_curtain(void) {
    draw_chrome();
    uint64_t t = monotonic_ms() - g_curtain_since;
    if (t > 250) {
        float in = fminf(1.0f, (float)(t - 250) / 220.0f);
        float p = (float)((t - 250) % 1100) / 1100.0f, e = p * p * (3 - 2 * p);
        float len = 220, x = BODY_X0 - len + e * (BODY_X1 - BODY_X0 + len);
        float x0 = fmaxf(BODY_X0, x), x1 = fminf(BODY_X1, x + len);
        if (x1 > x0) {
            uint32_t a = (uint32_t)(255 * in) << 24;
            ui_rect(x0, BODY_Y1 - 8, x1 - x0, 5, (RED & 0x00ffffffu) | a);
        }
    }
}

/* ---- screens ---- */

static void draw_title(void) {
    if (!g_title.tex) {
        return;
    }
    ui_image(&g_title, 0, 0, 1280, 720, 1.0f);
    text(input_has_controller() ? "PRESS A TO START" : "CLICK TO START", 640, 660, 29, PAPER, 900,
         false, UI_CENTER);
}

static void draw_main(void) {
    draw_chrome();
    kit_page_title("MAIN MENU");
    for (int i = 0; i < g_main_count; ++i) {
        draw_tile(&g_main[i], false);
    }
    hint(hint_select_back(), true);
}

static const char *map_display_name(void) {
    extern const char *reimagined_map(void);
    const char *map = reimagined_map();
    return !map || !map[0] ? NULL : strstr(map, "kino") || strstr(map, "theater") ? "KINO DER TOTEN"
           : strstr(map, "ascension") ? "ASCENSION" : strstr(map, "callofthedead") ? "CALL OF THE DEAD"
           : strstr(map, "deadops") ? "DEAD OPS ARCADE" : strstr(map, "tutorial") ? "TUTORIAL" : NULL;
}

static int map_scene(void) {
    extern const char *reimagined_map(void);
    const char *map = reimagined_map();
    return !map ? -1 : strstr(map, "deadops") ? 1 : strstr(map, "ascension") ? 2
           : strstr(map, "callofthedead") ? 3 : strstr(map, "kino") || strstr(map, "theater") ? 0 : -1;
}

/*
 * Co-op pause: the game's own screen stays whole and working. Only this small box goes on top,
 * with the map, the round and the timer button.
 */
enum { COOP_BOX_X = 448, COOP_BOX_Y = 26, COOP_BOX_W = 410, COOP_BOX_H = 112 };

static const struct button *coop_timer_button(const struct style *s) {
    for (int i = 0; i < s->button_count; ++i) {
        if (s->buttons[i].timer) {
            return &s->buttons[i];
        }
    }
    return NULL;
}

static void coop_timer_bounds(struct button *b) {
    b->x = COOP_BOX_X + 12;
    b->y = COOP_BOX_Y + 62;
    b->w = COOP_BOX_W - 24;
    b->h = 40;
}

static void draw_coop_pause_page(const struct style *s) {
    extern int reimagined_round(void);
    extern const char *reimagined_map(void);
    float pts[8];
    comic_pts(COOP_BOX_X, COOP_BOX_Y, COOP_BOX_W, COOP_BOX_H, 3, pts);
    ui_quad(pts, 0xf00d0c0bu);
    comic_outline(pts);
    const char *name = map_display_name();
    int round = reimagined_round();
    const char *map = reimagined_map();
    char line[64] = "";
    if (name && round > 0 && !(map && strstr(map, "deadops"))) {
        snprintf(line, sizeof(line), "%s - ROUND %d", name, round);
    } else if (name) {
        snprintf(line, sizeof(line), "%s", name);
    }
    text(line, COOP_BOX_X + 16, COOP_BOX_Y + 44, 25, PAPER, COOP_BOX_W - 32, true, UI_LEFT);
    const struct button *t = coop_timer_button(s);
    if (t) {
        struct button b = *t;
        coop_timer_bounds(&b);
        draw_tile(&b, false);
    }
}

static void draw_page(const struct style *s, const char *id) {
    bool pause = strcmp(id, "pause") == 0;
    if (coop_native_pause(id)) {
        draw_coop_pause_page(s);
        return;
    }
    if (s->message_count) {
        kit_burst();
        float plate[8];
        comic_pts(90, 250, 1100, 60 + (float)s->message_count * 56, 6, plate);
        ui_quad(plate, 0xe80d0c0bu);
        comic_outline(plate);
        for (int i = 0; i < s->message_count; ++i) {
            text(s->message[i], 640, 300 + (float)i * 56, 38, PAPER, 1080, true, UI_CENTER);
        }
        for (int i = 0; i < s->button_count; ++i) {
            draw_tile(&s->buttons[i], false);
        }
        hint(hint_select_back(), true);
        return;
    }
    if (s->has_window) {
        ui_mask_begin();
        ui_rect(s->window[0], s->window[1], s->window[2], s->window[3], 0xffffffffu);
        ui_mask_use(false);
    }
    draw_chrome();
    if (pause) {
        int scene = map_scene();
        if (scene >= 0 && g_scenes[scene].tex) {
            kit_cover(&g_scenes[scene], BODY_X0 + 6, BODY_Y0 + 6, BODY_X1 - BODY_X0 - 12, BODY_Y1 - BODY_Y0 - 12, 0x8c000000u);
        }
    }
    if (s->has_window) {
        ui_mask_end();
        ui_frame(s->window[0] - 3, s->window[1] - 3, s->window[2] + 6, s->window[3] + 6, 6, INK);
        ui_frame(s->window[0] - 1, s->window[1] - 1, s->window[2] + 2, s->window[3] + 2, 1.5f, EDGE);
    }
    kit_page_title(s->title);
    for (int i = 0; i < s->button_count; ++i) {
        draw_tile(&s->buttons[i], false);
    }
    if (pause) {
        extern int reimagined_round(void);
        extern const char *reimagined_map(void);
        char line[96];
        text(host_player_name(), 196, 196, 44, PAPER, 640, true, UI_LEFT);
        const char *map = reimagined_map();
        const char *name = map_display_name();
        int round = reimagined_round();
        snprintf(line, sizeof(line), "%s%s", name ? name : "", name && round > 0 ? "   -   " : "");
        if (round > 0 && !(map && strstr(map, "deadops"))) {
            size_t n = strlen(line);
            snprintf(line + n, sizeof(line) - n, "ROUND %d", round);
        }
        if (line[0]) {
            text(line, 198, 236, 26, 0xfff5c518u, 640, true, UI_LEFT);
        }
    }
    hint(!s->hint[0] || strcmp(id, "gameover") == 0 ? hint_select_back()
         : input_has_controller() ? "D-PAD: NAVIGATE     A: SELECT     START: RESUME"
                                  : "CLICK: SELECT     ESC: RESUME",
         true);
}

/* The game's own page stays: it is the reference. Only the shared footer goes on top. */
static void draw_fps_plates(void) {
    char line[48], value[16];
    int y = FPS_PLATE_Y;
    if (g_opt.fps_limit > 0) {
        snprintf(value, sizeof(value), "%d", g_opt.fps_limit);
    } else {
        snprintf(value, sizeof(value), "UNLIMITED");
    }
    snprintf(line, sizeof(line), "FPS LIMIT: %s", value);
    kit_panel(FPS_PLATE_X, y, FPS_PLATE_W, FPS_PLATE_H, NULL, line, "", false, false);
    y += FPS_PLATE_H + 8;
    if (g_opt.low_fps_paused > 0) {
        snprintf(value, sizeof(value), "%d", g_opt.low_fps_paused);
    } else {
        snprintf(value, sizeof(value), "OFF");
    }
    snprintf(line, sizeof(line), "LOW FPS WHILE MATCH PAUSED: %s", value);
    kit_panel(FPS_PLATE_X, y, FPS_PLATE_W, FPS_PLATE_H, NULL, line, "", false, false);
}

static void draw_frame(const struct style *s) {
    (void)s;
    if (screens_is("pausesettings")) {
        draw_fps_plates();
    }
    hint(hint_select_back(), false);
}

/* The portraits are wedges cut to interlock, as in the game's selection: square art, kept square. */
static const struct char_slot {
    float y, hit_y, hit_h, name_x, name_y;
} k_chars[4] = {{74, 74, 104, 214, 108},
                {150, 178, 100, 106, 236},
                {288, 288, 100, 106, 326},
                {364, 388, 112, 218, 468}};
static const float CHAR_X = 86, CHAR_W = 290, CHAR_H = 190;

static float k_chars_hit_y(int i) { return k_chars[i].hit_y; }
static float k_chars_hit_h(int i) { return k_chars[i].hit_h; }

static void map_panel_rect(int i, float *x, float *y, float *w, float *h) {
    *w = 196;
    *h = 330;
    *x = 400 + (float)i * 202;
    *y = 112 - (i % 2 ? 4.0f : 0.0f);
}

static void draw_charselect(void) {
    int map = g_picked_map < 1 ? 1 : g_picked_map > 4 ? 4 : g_picked_map;
    int sel = map - 1;
    bool arcade = map == 2;
    kit_sheet(true);
    static const char *const names[4][2] = {
        {"NIKOLAI", "BELINSKI"}, {"TAKEO", "MASAKI"}, {"DR. EDWARD", "RICHTOFEN"}, {"TANK", "DEMPSEY"}};
    /* drawn bottom-up so each wedge's point stays on top of the next one */
    for (int i = 3; i >= 0; --i) {
        const struct char_slot *c = &k_chars[i];
        ui_image(&g_portraits[i], CHAR_X, c->y, CHAR_W, CHAR_H, arcade ? 0.35f : 1.0f);
    }
    for (int i = 0; i < 4; ++i) {
        const struct char_slot *c = &k_chars[i];
        uint32_t color = arcade ? 0xffaaa69du : g_picked_char == i ? 0xfff5c518u : PAPER;
        static const char *const ids[4] = {"nikolai", "takeo", "richtofen", "dempsey"};
        char layer[32];
        snprintf(layer, sizeof(layer), "%s name 1", ids[i]);
        placed_text(layer, names[i][0], c->name_x, c->name_y, 23, color, 170, true, UI_LEFT);
        snprintf(layer, sizeof(layer), "%s name 2", ids[i]);
        placed_text(layer, names[i][1], c->name_x, c->name_y + 25, 23, color, 170, true, UI_LEFT);
        if (g_picked_char == i && !arcade) {
            ui_image(&g_tick, c->name_x + 112, c->name_y - 2, 38, 38, 1.0f);
        }
    }
    for (int i = 0; i < 4; ++i) {
        float x, y, w, h;
        map_panel_rect(i, &x, &y, &w, &h);
        kit_panel(x, y, w, h, &g_maps[i], k_map_names[i], "", sel == i, sel != i);
        if (sel == i) {
            ui_image(&g_tick, x + w - 52, y + 10, 42, 42, 1.0f);
        }
    }
    kit_strip(58, 4, 470, "SELECT CHARACTER", "character title");
    kit_strip(700, 2, 400, "SELECT MAP", "map title");
    bool blocked = g_map_busy || g_map_failed || g_launching;
    kit_arrow(true, 76, 548, 240, 116, "BACK", "back", false);
    kit_arrow(false, 960, 548, 240, 116, "START", "start", blocked);
    const char *status = g_launching ? "STARTING GAME..." : g_map_failed ? "SELECT THE MAP AGAIN"
                         : g_map_busy ? "PREPARING MAP..." : arcade ? "ARCADE SQUAD" : "READY TO SURVIVE!";
    text(status, 640, 590, 38, PAPER, 560, true, UI_CENTER);
    char player[64];
    snprintf(player, sizeof(player), "PLAYER: %s", host_player_name());
    text(player, 640, 628, 24, SAND, 560, true, UI_CENTER);
    hint(hint_select_back(), true);
}

static void draw_name_editor(void) {
    ui_rect(0, 0, 1280, 720, 0xc0000000u);
    float pts[8];
    comic_pts(290, 230, 700, 250, 7, pts);
    ui_quad(pts, 0xff0d0c0bu);
    comic_outline(pts);
    g_layout_screen = "name_editor";
    kit_strip(310, 196, 420, "PLAYER NAME", "title");
    char shown[24];
    snprintf(shown, sizeof(shown), "%s%s", g_name, (monotonic_ms() / 450) % 2 ? "_" : "");
    ui_rect(320, 320, 640, 70, 0xff231d1bu);
    text(shown, 340, 372, 44, 0xfff5c518u, 600, true, UI_LEFT);
    text("TYPE A NAME     ENTER: SAVE     ESC: CANCEL", 640, 445, 20, SAND, 640, false, UI_CENTER);
}

/* ---- character / map selection: drive the game's carousel like Cursor.java ---- */

static void pick_map(int index) {
    if (g_launching) {
        return;
    }
    g_picked_map = index;
    g_start_requested = false;
    g_apply_char_at = 0;
    g_start_at = 0;
    /* Finish observing an in-flight arrow even when the requested map changes.
     * Dropping it loses the carousel position and can drive into the end stop. */
    if (g_map_busy && !g_map_failed) {
        return;
    }
    if (g_map_failed) {
        g_carousel_at = -1;
    }
    if (!g_map_busy || !g_map_deadline) {
        g_map_deadline = monotonic_ms() + 20000;
    }
    g_map_failed = false;
    g_map_busy = true;
    g_home_misses = 0;
    g_arrow_tap_at = 0;
    g_next_step = monotonic_ms() + 900; /* Let the native selection page settle. */
}

static void arrow_tap(int dir) {
    screens_art_signature(g_art_before);
    g_arrow_dir = dir;
    g_arrow_tap_at = monotonic_ms();
    if (dir < 0) {
        input_tap(447, 490);
    } else {
        input_tap(845, 495);
    }
}

static void apply_character(void) {
    static const float ys[4] = {156, 263, 380, 496};
    if (!g_map_busy && !g_map_failed && g_picked_map != 2) {
        input_tap(297, ys[g_picked_char]);
    }
}

static void selection_step(void) {
    uint64_t now = monotonic_ms();
    if (g_apply_char_at && now >= g_apply_char_at && !input_tap_busy()) {
        g_apply_char_at = 0;
        apply_character();
    }
    if (g_start_at && now >= g_start_at && !input_tap_busy()) {
        g_start_at = 0;
        if (!g_map_busy && !g_map_failed) {
            input_tap(980, 630);
        }
    }
    if (g_launching && now > g_launch_until) {
        /* still on this page: the game missed the touch (it ignores them while animating) */
        if (g_start_retries < 2 && !input_tap_busy()) {
            g_start_retries++;
            log_info("menus: START not taken, trying again");
            input_tap(980, 630);
            g_launch_until = now + 1800;
        } else if (g_start_retries >= 2) {
            g_launching = false;
        }
    }
    if (!g_map_busy || now < g_next_step || input_tap_busy()) {
        return;
    }
    g_next_step = now + 150;
    if (now > g_map_deadline) {
        g_arrow_tap_at = 0;
        g_map_busy = false;
        g_map_failed = true;
        g_start_requested = false;
        log_info("menus: map selection timed out");
        return;
    }
    if (g_arrow_tap_at) {
        int sig[48];
        screens_art_signature(sig);
        float diff = 0;
        for (int i = 0; i < 48; ++i) {
            diff += (float)abs(sig[i] - g_art_before[i]);
        }
        diff /= 48;
        if (diff > 12) {
            g_home_misses = 0;
            g_map_deadline = now + 20000; /* Timeout measures lack of progress, not slow loading. */
            if (g_carousel_at >= 0) {
                g_carousel_at += g_arrow_dir;
            }
            g_arrow_tap_at = 0;
            g_next_step = now + 450;
            return;
        }
        if (now - g_arrow_tap_at < 2500) {
            return;
        }
        g_arrow_tap_at = 0;
        if (g_carousel_at < 0 && g_arrow_dir < 0) {
            /* A single ignored touch during loading is not proof of an end
             * stop. Confirm with another completed, settled left attempt. */
            if (++g_home_misses < 2) {
                arrow_tap(-1);
                return;
            }
            g_carousel_at = 0; /* tutorial: confirmed left end */
        } else {
            arrow_tap(g_arrow_dir);
            return;
        }
    }
    if (g_carousel_at < 0) {
        arrow_tap(-1);
    } else if (g_carousel_at != g_picked_map) {
        arrow_tap(g_picked_map > g_carousel_at ? 1 : -1);
    } else {
        g_map_busy = false;
        g_apply_char_at = now + 200;
    }
}

static void start_selected(void) {
    if (g_map_failed || g_launching) {
        return;
    }
    if (g_map_busy) {
        g_start_requested = true;
        return;
    }
    g_start_requested = false;
    g_launching = true;
    g_start_retries = 0;
    g_apply_char_at = 0;
    apply_character();
    g_start_at = monotonic_ms() + 300;
    g_launch_until = monotonic_ms() + 1800;
}

/* ---- frame logic ---- */

void menus_frame(void) {
    if (!menus_enabled()) {
        return;
    }
    const char *screen = screens_current();
    bool match_active = reimagined_match_active();
    bool newly_unknown = !screen && !g_screen_unknown;
    g_screen_unknown = !screen;
    /* a probe that fails during a Flash animation would flash the original menu: hold a curtain */
    if (newly_unknown && g_prev_screen && !g_curtain && !match_active &&
        !in_match_menu(g_prev_screen) && strcmp(g_prev_screen, "charselect") != 0) {
        const struct style *prev = style_of(g_prev_screen);
        /* only the pages drawn here would flash the Flash menu underneath */
        if (strcmp(g_prev_screen, "main") == 0 || (prev && prev->page)) {
            begin_curtain(g_prev_screen, 0, 0);
        }
    }
    bool left_menus = !screen && match_active && g_prev_screen;
    bool changed = note_screen(screen, match_active);
    if (changed) {
        extern bool reimagined_rejoin_available(void);
        log_debug("rejoin flag on %s: %d", screen, reimagined_rejoin_available());
    }
    if (changed && strcmp(screen, "charselect") == 0) {
        g_carousel_at = -1;
        g_map_failed = false;
        g_launching = false;
        pick_map(g_picked_map);
    }
    if (changed || left_menus) {
        g_pending_target = -1;
        g_pending_screen = NULL;
        if (input_tap_busy()) {
            input_cancel_tap();
        }
        g_focus = -1;
        g_hover = -1;
    }
    update_curtain(screen);
    if (screen && strcmp(screen, "charselect") == 0) {
        selection_step();
        if (g_start_requested && !g_map_busy && !g_map_failed) {
            start_selected();
        }
    } else if (screen || match_active) {
        g_map_busy = false;
        g_start_requested = false;
    }
    build_targets(screen);
    if (g_focus >= g_target_count) {
        g_focus = -1;
    }
    g_focus_screen = screen;
    if (g_pending_target >= 0 && screen == g_pending_screen && !input_tap_busy() &&
        monotonic_ms() - screens_changed_ms() >= 900) {
        int target = g_pending_target;
        g_pending_target = -1;
        g_pending_screen = NULL;
        activate(target);
    }
}

void menus_draw(void) {
    if (!menus_enabled()) {
        return;
    }
    const char *screen = screens_current();
    if (getenv("CODBOZ_HIDE_OVERLAY")) {
        g_drawn_screen = screen;
        return;
    }
    if (g_curtain) {
        draw_curtain();
        return;
    }
    if (!screen) {
        g_drawn_screen = NULL;
        return;
    }
    g_drawn_screen = screen;
    g_layout_screen = strcmp(screen, "pause") == 0 ? "pause_solo" : screen;
    const struct style *s = style_of(screen);
    if (strncmp(screen, "title", 5) == 0) {
        draw_title();
    } else if (strcmp(screen, "main") == 0) {
        draw_main();
    } else if (strcmp(screen, "charselect") == 0) {
        draw_charselect();
    } else if (s && s->page) {
        draw_page(s, screen);
    } else if (s) {
        draw_frame(s);
    }
    if (g_pad_mode) {
        draw_focus();
    } else if (g_hover >= 0 && g_hover < g_target_count) {
        /* the mouse gets the same brackets over what it would press */
        int saved = g_focus;
        g_focus = g_hover;
        draw_focus();
        g_focus = saved;
    }
    if (g_name_open) {
        draw_name_editor();
    }
}

/* ---- input ---- */

static void activate(int index) {
    if (index < 0 || index >= g_target_count) {
        return;
    }
    const struct target t = g_targets[index];
    const char *screen = screens_current();
    switch (t.kind) {
    case T_NAME:
        g_pending_target = -1;
        g_name_open = true;
        snprintf(g_name, sizeof(g_name), "%s", host_player_name());
        SDL_StartTextInput();
        break;
    case T_TIMER:
        reimagined_toggle_timer();
        break;
    case T_FPS: {
        static const int limits[6] = {5, 15, 30, 60, 120, 0};
        int at = 3;
        for (int i = 0; i < 6; ++i) {
            if (limits[i] == g_opt.fps_limit) {
                at = i;
            }
        }
        g_opt.fps_limit = limits[(at + 1) % 6];
        settings_save();
        log_info("fps limit: %d", g_opt.fps_limit);
        break;
    }
    case T_LOW_FPS:
        g_opt.low_fps_paused = g_opt.low_fps_paused == 0 ? 5 : g_opt.low_fps_paused == 5 ? 15 : 0;
        settings_save();
        break;
    case T_CHAR:
        if (g_picked_map != 2) {
            g_picked_char = t.index;
            if (!g_map_busy) {
                apply_character();
            }
        }
        break;
    case T_MAP:
        pick_map(t.index);
        break;
    case T_START:
        start_selected();
        break;
    case T_TAP:
        if (input_tap_busy() || monotonic_ms() - screens_changed_ms() < 900) {
            g_pending_screen = screen;
            g_pending_target = index;
            break;
        }
        log_debug("menus: %s -> tap %.0f,%.0f", screen ? screen : "-", t.tx, t.ty);
        input_tap(t.tx, t.ty);
        /* page screens hide the Flash animation behind the curtain */
        if (t.to[0] && strcmp(t.to, "stay") != 0 && !t.to_game && screen &&
            strncmp(screen, "title", 5) != 0 && !coop_native_pause(screen)) {
            begin_curtain(screen, t.tx, t.ty);
        }
        break;
    }
}

void menus_hover(float x, float y) {
    if (!menus_enabled()) {
        return;
    }
    g_hover = target_at(x, y);
}

bool menus_pointer_passthrough(float x, float y) {
    if (!menus_enabled()) return true;
    if (g_name_open || g_curtain) return false;
    const char *screen = screens_current();
    if (!screen) return true;
    if (coop_native_pause(screen)) {
        return !(x >= COOP_BOX_X && x < COOP_BOX_X + COOP_BOX_W && y >= COOP_BOX_Y && y < COOP_BOX_Y + COOP_BOX_H);
    }
    const struct style *s = style_of(screen);
    bool drawn = strncmp(screen, "title", 5) == 0 || strcmp(screen, "main") == 0 ||
                 strcmp(screen, "charselect") == 0 || (s && s->page);
    if (!drawn) {
        return !(strcmp(screen, "pausesettings") == 0 && x >= FPS_PLATE_X && x < FPS_PLATE_X + FPS_PLATE_W &&
                 y >= FPS_PLATE_Y && y < FPS_PLATE_Y + 2 * FPS_PLATE_H + 8);
    }
    return s && s->has_window && x >= s->window[0] && x < s->window[0] + s->window[2] &&
           y >= s->window[1] && y < s->window[1] + s->window[3];
}

bool menus_click(float x, float y) {
    if (!menus_enabled()) {
        return false;
    }
    if (g_name_open || g_curtain || input_tap_busy()) {
        return true;
    }
    const char *screen = screens_current();
    if (!screen) {
        return false;
    }
    /* Input may arrive between recognition and drawing. Never hit targets left
     * over from the previous page or expose the Flash controls beneath it. */
    if (screen != g_drawn_screen || screen != g_focus_screen) {
        return true;
    }
    /* Page changes cancel the touch before it can activate the next menu. */
    if (strncmp(screen, "title", 5) == 0) {
        if (!input_tap_busy()) {
            input_tap(640, 650);
        }
        return true;
    }
    if (coop_native_pause(screen)) {
        /* the box takes its clicks; everything else is the game's own screen */
        if (x >= COOP_BOX_X && x < COOP_BOX_X + COOP_BOX_W && y >= COOP_BOX_Y && y < COOP_BOX_Y + COOP_BOX_H) {
            int box_hit = target_at(x, y);
            if (box_hit >= 0 && g_targets[box_hit].kind == T_TIMER) {
                activate(box_hit);
            }
            return true;
        }
        return false;
    }
    const struct style *s = style_of(screen);
    bool drawn = strcmp(screen, "main") == 0 || strcmp(screen, "charselect") == 0 || (s && s->page);
    if (!drawn) {
        int plate = target_at(x, y);
        if (plate >= 0 && (g_targets[plate].kind == T_FPS || g_targets[plate].kind == T_LOW_FPS)) {
            activate(plate);
            return true;
        }
        return false; /* frame-only screens: the Flash menu takes the click */
    }
    int hit = target_at(x, y);
    if (hit >= 0) {
        g_focus = hit;
        activate(hit);
    }
    /* outside the buttons a drawn page swallows the click, except its live windows */
    if (hit < 0 && s && s->has_window && x >= s->window[0] && x < s->window[0] + s->window[2] &&
        y >= s->window[1] && y < s->window[1] + s->window[3]) {
        return false;
    }
    return true;
}

bool menus_nav(int dir) {
    if (!menus_enabled() || !screens_current() || g_curtain || !g_target_count) {
        return false;
    }
    if (g_name_open) {
        return true;
    }
    if (g_focus < 0) {
        g_focus = 0;
        return true;
    }
    const struct target *cur = &g_targets[g_focus];
    float cx = cur->x + cur->w / 2, cy = cur->y + cur->h / 2;
    int best = -1;
    float best_score = 1e9f;
    for (int i = 0; i < g_target_count; ++i) {
        if (i == g_focus) {
            continue;
        }
        const struct target *t = &g_targets[i];
        float dx = t->x + t->w / 2 - cx, dy = t->y + t->h / 2 - cy, along, across;
        switch (dir) {
        case 0:
            along = -dx;
            across = dy;
            break;
        case 1:
            along = dx;
            across = dy;
            break;
        case 2:
            along = -dy;
            across = dx;
            break;
        default:
            along = dy;
            across = dx;
            break;
        }
        if (along <= 10) {
            continue;
        }
        float score = along + 2.5f * fabsf(across);
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    if (best >= 0) {
        g_focus = best;
    }
    return true;
}

bool menus_activate(void) {
    if (!menus_enabled() || !screens_current() || g_curtain) {
        return false;
    }
    if (g_name_open) {
        menus_name_key(40 /* SDL_SCANCODE_RETURN */);
        return true;
    }
    if (g_focus < 0) {
        return false;
    }
    activate(g_focus);
    return true;
}

bool menus_back(void) {
    if (!menus_enabled() || !screens_current() || g_curtain) {
        return false;
    }
    if (g_name_open) {
        menus_name_key(41 /* SDL_SCANCODE_ESCAPE */);
        return true;
    }
    const char *screen = screens_current();
    if (strcmp(screen, "charselect") == 0) {
        activate(g_target_count - 1); /* BACK */
        return true;
    }
    if (coop_native_pause(screen)) {
        input_tap(200, 633); /* the native RETURN */
        return true;
    }
    const struct style *s = style_of(screen);
    if (s && s->page) {
        for (int i = 0; i < g_target_count; ++i) {
            const struct button *b = &s->buttons[g_targets[i].index];
            if (strcmp(b->id, "back") == 0 || strcmp(b->id, "no") == 0 || b->to_game ||
                (s->back[0] && strcmp(b->to, s->back) == 0)) {
                activate(i);
                return true;
            }
        }
    }
    return false; /* the game's own back key */
}

bool menus_editing_name(void) {
    return g_name_open;
}

void menus_name_text(const char *text_in) {
    for (const char *p = text_in; *p && strlen(g_name) < 13; ++p) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '.') {
            size_t n = strlen(g_name);
            g_name[n] = (char)toupper(c);
            g_name[n + 1] = 0;
        }
    }
}

void menus_name_key(int scancode) {
    if (scancode == 42 /* BACKSPACE */ && g_name[0]) {
        g_name[strlen(g_name) - 1] = 0;
    } else if (scancode == 40 || scancode == 88 /* RETURN, KP_ENTER */) {
        if (g_name[0]) {
            /* like the TV: the new name is used from the next start of the game */
            settings_set_player_name(g_name);
            extern void reimagined_write_name(void);
            reimagined_write_name();
        }
        g_name_open = false;
        SDL_StopTextInput();
    } else if (scancode == 41 /* ESCAPE */) {
        g_name_open = false;
        SDL_StopTextInput();
    }
}
