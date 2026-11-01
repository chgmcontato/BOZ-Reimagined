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
static struct ui_image g_portraits[4], g_scenes[4];
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

void menus_init(const char *assets_dir) {
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
    }
    g_enabled = g_paper.tex != 0;
    log_info("reimagined menus: %s (%d styled screens)", g_enabled ? "on" : "off", g_style_count);
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

static void comic_pts(float x, float y, float w, float h, float skew, float out[8]) {
    float pts[8] = {x, y + skew, x + w, y, x + w - 3, y + h - skew, x + 2, y + h};
    memcpy(out, pts, sizeof(pts));
}

static void comic_outline(const float pts[8]) {
    ui_quad_outline(pts, 5, INK);
    ui_quad_outline(pts, 1.5f, EDGE);
}

/* Paper, title strip and ink body: the same on every page (and the main menu). */
static void draw_chrome(void) {
    ui_rect(0, 0, 1280, 720, PAPER);
    ui_image(&g_paper, 0, 0, 1280, 720, 1.0f);
    ui_rect(48, 134, 1184, 540, INK);
    ui_rect(64, 38, 1152, 96, INK);
    text("BLACK OPS ZOMBIES", 86, 80, 30, PAPER, 660, false, UI_LEFT);
    text("REIMAGINED", 85, 119, 42, PAPER, 660, false, UI_LEFT);
}

static void hint(const char *s, bool on_paper) {
    if (!on_paper) {
        ui_rect(380, 684, 520, 30, INK);
    }
    text(s, 640, on_paper ? 698 : 706, on_paper ? 20 : 17, on_paper ? INK : PAPER, 780, false,
         UI_CENTER);
}

static void draw_tile(const struct button *b, bool primary_red) {
    float pts[8];
    comic_pts(b->x, b->y, b->w, b->h, b->h > 100 ? 6 : 4, pts);
    ui_mask_begin();
    ui_quad(pts, 0xffffffffu);
    ui_mask_use(true);
    ui_rect(b->x, b->y, b->w, b->h, b->primary && !b->art ? RED : DIM);
    if (b->art && b->art->tex) {
        ui_image_cover(b->art, b->x, b->y, b->w, b->h - 100, 1.0f);
    }
    if (b->caption[0]) {
        float top = b->y + b->h - 112;
        ui_rect(b->x, top, b->w, b->y + b->h - top, primary_red ? RED : INK);
        text(b->title, b->x + 16, top + 44, 38, PAPER, b->w - 32, false, UI_LEFT);
        text(b->caption, b->x + 16, top + 85, 19, SAND, b->w - 32, false, UI_LEFT);
    } else {
        float size = fminf(31, b->h * 0.42f);
        char title[64];
        if (b->timer) {
            extern bool reimagined_timer_shown(void);
            snprintf(title, sizeof(title), "TIMER: %s", reimagined_timer_shown() ? "ON" : "OFF");
        } else if (strcmp(b->to, "name") == 0) {
            snprintf(title, sizeof(title), "PLAYER: %s", host_player_name());
        } else {
            snprintf(title, sizeof(title), "%s", b->title);
        }
        text(title, b->x + 22, b->y + b->h / 2 + size * 0.36f, size, PAPER, b->w - 44, false,
             UI_LEFT);
    }
    ui_mask_end();
    comic_outline(pts);
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

/* pausecoop's buttons (return, settings, leave, timer, fps) over the native art, 1280x720. */
static void coop_pause_bounds(int index, struct button *b) {
    static const float bounds[5][4] = {{115, 593, 222, 97},
                                       {279, 166, 177, 181},
                                       {579, 380, 183, 144},
                                       {400, 630, 170, 56},
                                       {582, 630, 150, 56}};
    b->x = bounds[index][0];
    b->y = bounds[index][1];
    b->w = bounds[index][2];
    b->h = bounds[index][3];
}

static void add_buttons(const struct button *buttons, int count) {
    for (int i = 0; i < count; ++i) {
        const struct button *b = &buttons[i];
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
            add_target(86, 164 + (float)i * 100, 300, 94, 0, 0, T_CHAR, i, "", false);
        }
        for (int i = 0; i < 4; ++i) {
            add_target(850, 174 + (float)i * 77, 344, 64, 0, 0, T_MAP, i + 1, "", false);
        }
        add_target(952, 577, 254, 94, 0, 0, T_START, 0, "", false);
        add_target(80, 577, 215, 94, 267, 622, T_TAP, 0, "*", false);
    } else if (s && s->page && coop_native_pause(screen)) {
        for (int i = 0; i < s->button_count && i < 5; ++i) {
            struct button b = s->buttons[i];
            coop_pause_bounds(i, &b);
            add_buttons(&b, 1);
            g_targets[g_target_count - 1].index = i;
        }
    } else if (s && s->page) {
        add_buttons(s->buttons, s->button_count);
    } else {
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
        float len = 220, x = 64 - len + e * (1152 + len);
        float x0 = fmaxf(64, x), x1 = fminf(1216, x + len);
        if (x1 > x0) {
            uint32_t a = (uint32_t)(255 * in) << 24;
            ui_rect(x0, 129, x1 - x0, 5, (RED & 0x00ffffffu) | a);
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
    text("MAIN MENU", 942, 100, 33, PAPER, 250, false, UI_LEFT);
    for (int i = 0; i < g_main_count; ++i) {
        draw_tile(&g_main[i], strcmp(g_main[i].id, "single") == 0);
    }
    hint(hint_select_back(), true);
}

static const char *map_backdrop_index(void) {
    extern const char *reimagined_map(void);
    return reimagined_map();
}

/* The game's own co-op pause stays visible; only what it lacks is drawn on top. */
static void draw_coop_pause_page(const struct style *s) {
    extern const char *reimagined_map(void);
    extern int reimagined_round(void);
    const char *map = reimagined_map();
    const char *name = !map || !map[0] ? NULL : strstr(map, "kino") || strstr(map, "theater") ? "KINO DER TOTEN"
                       : strstr(map, "ascension") ? "ASCENSION" : strstr(map, "callofthedead") ? "CALL OF THE DEAD"
                       : strstr(map, "deadops") ? "DEAD OPS ARCADE" : NULL;
    int round = reimagined_round();
    char line[96] = "";
    if (name && round > 0 && !strstr(map, "deadops")) {
        snprintf(line, sizeof(line), "%s   -   ROUND %d", name, round);
    } else if (name) {
        snprintf(line, sizeof(line), "%s", name);
    }
    if (line[0]) {
        float pts[8];
        comic_pts(84, 104, 440, 44, 3, pts);
        ui_quad(pts, INK);
        comic_outline(pts);
        text(line, 102, 134, 22, PAPER, 404, false, UI_LEFT);
    }
    for (int i = 3; i < s->button_count && i < 5; ++i) {
        struct button b = s->buttons[i];
        coop_pause_bounds(i, &b);
        draw_tile(&b, false);
    }
}

static void draw_page(const struct style *s, const char *id) {
    bool pause = strcmp(id, "pause") == 0;
    if (coop_native_pause(id)) {
        draw_coop_pause_page(s);
        return;
    }
    if (s->has_window) {
        ui_mask_begin();
        ui_rect(s->window[0], s->window[1], s->window[2], s->window[3], 0xffffffffu);
        ui_mask_use(false);
    }
    draw_chrome();
    if (pause) {
        /* the map's scenery as one panel behind the body, darkened for the text */
        const char *map = map_backdrop_index();
        int scene = !map ? -1 : strstr(map, "deadops") ? 1 : strstr(map, "ascension") ? 2
                                : strstr(map, "callofthedead") ? 3 : strstr(map, "kino") || strstr(map, "theater") ? 0 : -1;
        if (scene >= 0 && g_scenes[scene].tex) {
            const struct ui_image *b = &g_scenes[scene];
            float h = (float)b->w * 636.0f / 1184.0f, top = ((float)b->h - h) * 0.4f;
            ui_image_part(b, 48, 38, 1184, 636, 0, top / (float)b->h, 1, (top + h) / (float)b->h, 1.0f);
            ui_rect(48, 38, 1184, 636, 0x8c151211u);
            ui_rect(48, 38, 1184, 132, 0x60151211u);
            text("BLACK OPS ZOMBIES", 86, 80, 30, PAPER, 660, false, UI_LEFT);
            text("REIMAGINED", 85, 119, 42, PAPER, 660, false, UI_LEFT);
        }
    }
    if (s->has_window) {
        ui_mask_end();
        ui_frame(s->window[0] - 3, s->window[1] - 3, s->window[2] + 6, s->window[3] + 6, 6, INK);
        ui_frame(s->window[0] - 1, s->window[1] - 1, s->window[2] + 2, s->window[3] + 2, 1.5f, EDGE);
    }
    text(s->title, 1194, 100, 33, PAPER, 420, false, UI_RIGHT);
    for (int i = 0; i < s->message_count; ++i) {
        text(s->message[i], 640, 260 + (float)i * 56, 40, PAPER, 1080, true, UI_CENTER);
    }
    for (int i = 0; i < s->button_count; ++i) {
        draw_tile(&s->buttons[i], s->buttons[i].primary);
    }
    if (pause) {
        extern int reimagined_round(void);
        char line[96];
        text(host_player_name(), 196, 214, 44, PAPER, 640, false, UI_LEFT);
        const char *map = map_backdrop_index();
        const char *name = !map || !map[0] ? NULL : strstr(map, "kino") || strstr(map, "theater") ? "KINO DER TOTEN"
                           : strstr(map, "ascension") ? "ASCENSION" : strstr(map, "callofthedead") ? "CALL OF THE DEAD"
                           : strstr(map, "deadops") ? "DEAD OPS ARCADE" : strstr(map, "tutorial") ? "TUTORIAL" : NULL;
        int round = reimagined_round();
        snprintf(line, sizeof(line), "%s%s", name ? name : "", name && round > 0 ? "   -   " : "");
        if (round > 0 && !(map && strstr(map, "deadops"))) {
            size_t n = strlen(line);
            snprintf(line + n, sizeof(line) - n, "ROUND %d", round);
        }
        if (line[0]) {
            text(line, 198, 252, 26, SAND, 640, false, UI_LEFT);
        }
    }
    hint(!s->hint[0] ? hint_select_back()
         : input_has_controller() ? "D-PAD: NAVIGATE     A: SELECT     START: RESUME"
                                  : "CLICK: SELECT     ESC: RESUME",
         true);
}

static void draw_frame(const struct style *s) {
    if (!s->has_tag) {
        return;
    }
    float pts[8];
    comic_pts(s->tag[0], s->tag[1], s->tag[2], s->tag[3], 5, pts);
    ui_quad(pts, INK);
    comic_outline(pts);
    float size = fminf(44, s->tag[3] * 0.62f);
    text(s->title, s->tag[0] + 18, s->tag[1] + s->tag[3] / 2 + size * 0.36f, size, PAPER,
         s->tag[2] - 36, false, UI_LEFT);
    hint(hint_select_back(), false);
}

static void draw_charselect(void) {
    int map = g_picked_map < 1 ? 1 : g_picked_map > 4 ? 4 : g_picked_map;
    int sel = map - 1;
    bool arcade = map == 2;
    ui_rect(0, 0, 1280, 720, INK);
    ui_image(&g_paper, 0, 0, 1280, 720, 1.0f);
    ui_rect(48, 80, 1184, 582, INK);
    ui_image(&g_scenes[sel], 64, 96, 1152, 556, 1.0f);
    ui_rect(64, 96, 1152, 556, 0x63000000u);
    ui_image(&g_strip, 66, 34, 742, 119, 1.0f);
    text("SELECT CHARACTER", 103, 108, 39, INK, 700, false, UI_LEFT);
    ui_image(&g_strip, 837, 73, 374, 79, 1.0f);
    text("SELECT MAP", 869, 125, 28, INK, 330, false, UI_LEFT);
    static const char *const names[4][2] = {
        {"NIKOLAI", "BELINSKI"}, {"TAKEO", "MASAKI"}, {"DR. EDWARD", "RICHTOFEN"}, {"TANK", "DEMPSEY"}};
    for (int i = 0; i < 4; ++i) {
        float y = 164 + (float)i * 100;
        ui_image(&g_portraits[i], 86, y, 300, 94, 1.0f);
        if (arcade) {
            ui_rect(86, y, 300, 94, 0xb0151211u);
        }
        float x = (i == 0 || i == 3) ? 211 : 101, base = y + ((i == 0 || i == 3) ? 42 : 29);
        uint32_t c = arcade ? 0xffaaa69du : PAPER;
        text(names[i][0], x, base, 23, c, 200, true, UI_LEFT);
        text(names[i][1], x, base + 25, 23, c, 200, true, UI_LEFT);
        if (g_picked_char == i && !arcade) {
            ui_image(&g_tick, 350, y + 3, 34, 37, 1.0f);
        }
    }
    float pts[8];
    comic_pts(410, 155, 410, 399, 7, pts);
    ui_mask_begin();
    ui_quad(pts, 0xffffffffu);
    ui_mask_use(true);
    ui_rect(410, 155, 410, 399, INK);
    ui_image(&g_scenes[sel], 413, 155, 404, 399, 1.0f);
    ui_mask_end();
    ui_quad_outline(pts, 6, INK);
    ui_image(&g_strip, 418, 467, 392, 76, 1.0f);
    text(k_map_names[sel], 445, 515, 27, INK, 340, false, UI_LEFT);
    if (arcade) {
        text("ARCADE SQUAD", 459, 446, 23, PAPER, 340, true, UI_LEFT);
    }
    for (int i = 0; i < 4; ++i) {
        float y = 174 + (float)i * 77;
        comic_pts(850, y, 344, 64, i % 2 == 0 ? 4 : -3, pts);
        ui_quad(pts, sel == i ? RED : DIM);
        comic_outline(pts);
        text(k_map_names[i], 866, y + 41, 24, PAPER, 280, true, UI_LEFT);
        if (sel == i) {
            ui_image(&g_tick, 1151, y + 13, 36, 38, 1.0f);
        }
    }
    ui_image(&g_back, 80, 577, 215, 94, 1.0f);
    text("BACK", 113, 630, 33, PAPER, 180, true, UI_LEFT);
    ui_image(&g_start, 952, 577, 254, 94, 1.0f);
    bool blocked = g_map_busy || g_map_failed || g_launching;
    text("START", 984, 632, 38, blocked ? 0xffbcb8adu : PAPER, 200, true, UI_LEFT);
    const char *status = g_launching ? "STARTING GAME..." : g_map_failed ? "SELECT THE MAP AGAIN"
                         : g_map_busy ? "PREPARING MAP..." : "READY TO SURVIVE!";
    text(status, 408, 620, g_map_failed ? 22 : 25, PAPER, 520, true, UI_LEFT);
    char player[64];
    snprintf(player, sizeof(player), "PLAYER: %s", host_player_name());
    text(player, 408, 648, 19, SAND, 520, true, UI_LEFT);
    hint(hint_select_back(), true);
}

static void draw_name_editor(void) {
    ui_rect(0, 0, 1280, 720, 0xc0000000u);
    float pts[8];
    comic_pts(290, 230, 700, 250, 7, pts);
    ui_quad(pts, INK);
    comic_outline(pts);
    text("PLAYER NAME", 320, 290, 40, PAPER, 640, false, UI_LEFT);
    char shown[24];
    snprintf(shown, sizeof(shown), "%s%s", g_name, (monotonic_ms() / 450) % 2 ? "_" : "");
    ui_rect(320, 320, 640, 70, 0xff231d1bu);
    text(shown, 340, 372, 44, 0xfff5c518u, 600, false, UI_LEFT);
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
        g_launching = false;
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
    if (g_curtain) {
        draw_curtain();
        return;
    }
    if (!screen) {
        g_drawn_screen = NULL;
        return;
    }
    g_drawn_screen = screen;
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
            strncmp(screen, "title", 5) != 0) {
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
    const struct style *s = style_of(screen);
    bool drawn = strncmp(screen, "title", 5) == 0 || strcmp(screen, "main") == 0 ||
                 strcmp(screen, "charselect") == 0 || (s && s->page);
    if (!drawn) return true;
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
    const struct style *s = style_of(screen);
    bool drawn = strcmp(screen, "main") == 0 || strcmp(screen, "charselect") == 0 || (s && s->page);
    if (!drawn) {
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
