/*
 * Launcher: the first screen of codboz-pc. Laid out after Downhill Revived's launcher
 * (a rail with the wordmark, pages and the player card; plain rows in the main pane;
 * a footer with the status and one primary button whose label follows the state),
 * in the game's colours. Mouse, keyboard and controller all work; the gold focus
 * ring shows only after a key or pad press.
 */
#include "launcher.h"
#include "bindings.h"
#include "host.h"
#include "install.h"
#include "ui.h"

#include <SDL.h>
#include <ctype.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

SDL_Window *video_window(void);

enum {
    REF_W = 960,
    REF_H = 600,
    RAIL_W = 208,
    FOOT_H = 64,
};

/* palette: night rail, paper ink, blood accent, HUD gold */
#define C_RAIL 0xff120e0du
#define C_GROUND 0xff1b1615u
#define C_RAISE 0xff271f1du
#define C_RAISE_HI 0xff352a27u
#define C_ACCENT 0xffa8262fu
#define C_ACCENT_HI 0xffc23540u
#define C_GOLD 0xfff5c518u
#define C_SNOW 0xfff2eee4u
#define C_SAND 0xffd5c9acu
#define C_DIM 0xff8f8270u
#define C_LINE 0x2ed5c9acu
#define C_OK 0xff6fdc9cu
#define C_BAD 0xffff7a6bu

enum page { PAGE_PLAY, PAGE_SETTINGS, PAGE_CONTROLS, PAGE_COUNT, PAGE_START = 100 };

static enum page g_page;
static enum game_mode g_mode;
static char g_obb[1024];
static char g_message[256];
static bool g_message_bad;
static bool g_editing_name;
static char g_name[16];
static int g_capture_action = -1, g_capture_slot;

/* input state for the immediate-mode widgets */
static float g_mx, g_my;
static bool g_click, g_mouse_held;
static int g_focus, g_focus_count, g_focus_move;
static bool g_activate, g_kbd_mode, g_left, g_right;

/* Launcher text in the chosen language. */
#define TR(pt, en) (g_opt.english ? (en) : (pt))

static void cfg_path(char *out, size_t size, const char *name) {
    snprintf(out, size, "%s/%s", g_data_root, name);
}

static void load_cfg(void) {
    char path[1200];
    cfg_path(path, sizeof(path), "launcher.cfg");
    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }
    char line[1200];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strncmp(line, "obb=", 4) == 0) {
            snprintf(g_obb, sizeof(g_obb), "%s", line + 4);
        } else if (strncmp(line, "mode=", 5) == 0) {
            g_mode = strcmp(line + 5, "reimagined") == 0 ? MODE_REIMAGINED
                     : strcmp(line + 5, "ios") == 0      ? MODE_IOS
                                                         : MODE_ORIGINAL;
        }
    }
    fclose(f);
}

static void save_cfg(void) {
    char path[1200];
    cfg_path(path, sizeof(path), "launcher.cfg");
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "obb=%s\nmode=%s\n", g_obb, mode_dir_name(g_mode));
        fclose(f);
    }
}

void launcher_remember(int mode, const char *obb) {
    load_cfg();
    if (mode >= 0) {
        g_mode = (enum game_mode)mode;
    }
    if (obb) {
        snprintf(g_obb, sizeof(g_obb), "%s", obb);
    }
    save_cfg();
}

static bool obb_ok(void) {
    FILE *f = g_obb[0] ? fopen(g_obb, "rb") : NULL;
    if (f) {
        fclose(f);
    }
    return f != NULL;
}

/* The iOS mode carries its own data archive (installed from the IPA); the others need the OBB. */
static bool data_ok(void) {
    return g_mode == MODE_IOS || obb_ok();
}

bool launcher_saved_choice(enum game_mode *mode, char *obb, size_t obb_size) {
    load_cfg();
    *mode = g_mode;
    snprintf(obb, obb_size, "%s", g_obb);
    return data_ok() && install_present(g_data_root, g_mode);
}

/* Native "open file" dialog; returns false when cancelled. */
static bool pick_file(const char *title, const char *pattern, char *out, size_t size) {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char filter[128];
    snprintf(filter, sizeof(filter), "%s%c%s%cTodos%c*.*%c", pattern, 0, pattern, 0, 0, 0);
    memset(&ofn, 0, sizeof(ofn));
    out[0] = 0;
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = out;
    ofn.nMaxFile = (DWORD)size;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameA(&ofn) != 0;
#else
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "zenity --file-selection --title='%s' --file-filter='%s | %s' --file-filter='Todos | *' "
             "2>/dev/null || kdialog --getopenfilename . '%s' 2>/dev/null",
             title, pattern, pattern, pattern);
    FILE *p = popen(cmd, "r");
    if (!p) {
        return false;
    }
    bool ok = fgets(out, (int)size, p) != NULL;
    pclose(p);
    out[strcspn(out, "\r\n")] = 0;
    return ok && out[0];
#endif
}

static void message(bool bad, const char *text) {
    snprintf(g_message, sizeof(g_message), "%s", text);
    g_message_bad = bad;
}

/* ---- widgets ---- */

static bool hovered(float x, float y, float w, float h) {
    return g_mx >= x && g_mx < x + w && g_my >= y && g_my < y + h;
}

/* Registers a focusable widget; true when it is the focused one. */
static bool focusable(float x, float y, float w, float h) {
    int id = g_focus_count++;
    if (hovered(x, y, w, h) && !g_kbd_mode) {
        g_focus = id;
    }
    bool focused = g_focus == id;
    if (focused && g_kbd_mode) {
        ui_round_rect(x - 4, y - 4, w + 8, h + 8, 11, C_GOLD);
        ui_round_rect(x - 2, y - 2, w + 4, h + 4, 9, C_GROUND);
    }
    return focused;
}

static bool pressed(bool focused, float x, float y, float w, float h) {
    return (g_click && hovered(x, y, w, h)) || (focused && g_activate);
}

static void label(const char *text, float x, float baseline, float size, uint32_t color,
                  enum ui_align align) {
    ui_font(UI_FONT_BODY);
    ui_label(text, x, baseline, size, color, align);
}

enum icon { ICON_PLAY, ICON_SLIDERS, ICON_PAD, ICON_PENCIL, ICON_CHECK, ICON_ALERT };

static void icon(enum icon kind, float x, float y, float s, uint32_t c) {
    float t = s * 0.12f;
    switch (kind) {
    case ICON_PLAY:
        ui_triangle(x + s * 0.25f, y + s * 0.15f, x + s * 0.85f, y + s * 0.5f, x + s * 0.25f,
                    y + s * 0.85f, c);
        break;
    case ICON_SLIDERS:
        for (int i = 0; i < 3; ++i) {
            float lx = x + s * (0.2f + 0.3f * (float)i), knob = y + s * (i == 1 ? 0.35f : 0.65f);
            ui_line(lx, y + s * 0.1f, lx, y + s * 0.9f, t, c);
            ui_rect(lx - s * 0.13f, knob - t, s * 0.26f, t * 2, c);
        }
        break;
    case ICON_PAD:
        ui_round_rect(x, y + s * 0.22f, s, s * 0.56f, s * 0.22f, c);
        ui_rect(x + s * 0.17f, y + s * 0.46f, s * 0.24f, s * 0.08f, C_RAIL);
        ui_rect(x + s * 0.25f, y + s * 0.38f, s * 0.08f, s * 0.24f, C_RAIL);
        ui_circle(x + s * 0.7f, y + s * 0.42f, s * 0.06f, C_RAIL);
        ui_circle(x + s * 0.8f, y + s * 0.55f, s * 0.06f, C_RAIL);
        break;
    case ICON_PENCIL:
        ui_line(x + s * 0.2f, y + s * 0.8f, x + s * 0.8f, y + s * 0.2f, t * 1.6f, c);
        ui_triangle(x + s * 0.1f, y + s * 0.9f, x + s * 0.13f, y + s * 0.7f, x + s * 0.3f,
                    y + s * 0.87f, c);
        break;
    case ICON_CHECK:
        ui_line(x + s * 0.15f, y + s * 0.55f, x + s * 0.4f, y + s * 0.8f, t * 1.5f, c);
        ui_line(x + s * 0.4f, y + s * 0.8f, x + s * 0.88f, y + s * 0.22f, t * 1.5f, c);
        break;
    case ICON_ALERT:
        ui_circle(x + s * 0.5f, y + s * 0.5f, s * 0.45f, c);
        ui_rect(x + s * 0.45f, y + s * 0.22f, s * 0.1f, s * 0.36f, C_GROUND);
        ui_rect(x + s * 0.45f, y + s * 0.66f, s * 0.1f, s * 0.1f, C_GROUND);
        break;
    }
}

/* An ordinary button (secondary) or the one primary button (accent). */
static bool button(float x, float y, float w, float h, const char *text, bool primary, bool enabled) {
    bool focused = enabled && focusable(x, y, w, h);
    bool hover = enabled && (hovered(x, y, w, h) || (focused && g_kbd_mode));
    uint32_t fill = !enabled ? C_RAISE : primary ? (hover ? C_ACCENT_HI : C_ACCENT)
                                                 : (hover ? C_RAISE_HI : C_RAISE);
    ui_round_rect(x, y, w, h, 8, fill);
    label(text, x + w / 2, y + h / 2 + 5.5f, 15, enabled ? C_SNOW : C_DIM, UI_CENTER);
    return enabled && pressed(focused, x, y, w, h);
}

/* Segmented choice; returns the new index. */
static int segmented(float x, float y, float w, float h, const char *const *options, int count,
                     int value) {
    float seg = w / (float)count;
    ui_round_rect(x, y, w, h, 8, C_RAIL);
    for (int i = 0; i < count; ++i) {
        float sx = x + seg * (float)i;
        bool focused = focusable(sx, y, seg, h);
        if (i == value) {
            ui_round_rect(sx + 3, y + 3, seg - 6, h - 6, 6, C_RAISE_HI);
        }
        label(options[i], sx + seg / 2, y + h / 2 + 5, 14, i == value ? C_SNOW : C_DIM, UI_CENTER);
        if (pressed(focused, sx, y, seg, h)) {
            value = i;
        }
    }
    return value;
}

static bool toggle(float x, float y, bool value) {
    bool focused = focusable(x, y, 44, 24);
    ui_round_rect(x, y, 44, 24, 12, value ? C_ACCENT : C_RAISE_HI);
    ui_circle(value ? x + 32 : x + 12, y + 12, 8, C_SNOW);
    return pressed(focused, x, y, 44, 24) ? !value : value;
}

static float slider(float x, float y, float w, float value, float lo, float hi) {
    bool focused = focusable(x, y - 8, w, 24);
    float t = (value - lo) / (hi - lo);
    ui_round_rect(x, y, w, 6, 3, C_RAISE_HI);
    ui_round_rect(x, y, w * t, 6, 3, C_ACCENT);
    ui_circle(x + w * t, y + 3, 9, C_SNOW);
    if (g_mouse_held && hovered(x - 10, y - 12, w + 20, 30)) {
        t = (g_mx - x) / w;
    }
    if (focused && g_left) {
        t -= 0.05f;
    }
    if (focused && g_right) {
        t += 0.05f;
    }
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return lo + t * (hi - lo);
}

/* one row of a list: label left, separator line below */
static void row(float y, const char *name, const char *hint) {
    label(name, RAIL_W + 40, y + 22, 15, C_SNOW, UI_LEFT);
    if (hint) {
        label(hint, RAIL_W + 40, y + 40, 12, C_DIM, UI_LEFT);
    }
    ui_rect(RAIL_W + 40, y + 47, REF_W - RAIL_W - 80, 1, C_LINE);
}

/* ---- rail ---- */

static void write_name(void) {
    char clean[16];
    size_t n = 0;
    for (const char *p = g_name; *p && n < 13; ++p) {
        if (isalnum((unsigned char)*p) || *p == ' ' || *p == '-' || *p == '_' || *p == '.') {
            clean[n++] = (char)toupper((unsigned char)*p);
        }
    }
    clean[n] = 0;
    if (!n) {
        snprintf(clean, sizeof(clean), "PLAYER");
    }
    settings_set_player_name(clean);
    snprintf(g_name, sizeof(g_name), "%s", host_player_name());
}

static void stop_editing(void) {
    if (g_editing_name) {
        g_editing_name = false;
        SDL_StopTextInput();
        write_name();
    }
}

static void draw_rail(void) {
    ui_rect(0, 0, RAIL_W, REF_H, C_RAIL);
    ui_font(UI_FONT_DISPLAY);
    ui_label("BLACK OPS", 26, 58, 30, C_SNOW, UI_LEFT);
    ui_label("ZOMBIES", 26, 88, 30, C_ACCENT_HI, UI_LEFT);
    label("PC", 28, 110, 12, C_DIM, UI_LEFT);

    const char *const names[PAGE_COUNT] = {TR("Jogar", "Play"), TR("Configurações", "Settings"),
                                           TR("Controles", "Controls")};
    static const enum icon icons[PAGE_COUNT] = {ICON_PLAY, ICON_SLIDERS, ICON_PAD};
    for (int i = 0; i < PAGE_COUNT; ++i) {
        float y = 140 + (float)i * 44;
        bool focused = focusable(12, y, RAIL_W - 24, 40);
        bool on = g_page == (enum page)i;
        if (on) {
            ui_round_rect(12, y, RAIL_W - 24, 40, 8, C_RAISE);
        } else if (hovered(12, y, RAIL_W - 24, 40)) {
            ui_round_rect(12, y, RAIL_W - 24, 40, 8, 0x0affffffu);
        }
        icon(icons[i], 26, y + 11, 18, on ? C_ACCENT_HI : C_SAND);
        label(names[i], 56, y + 26, 15, on ? C_SNOW : C_SAND, UI_LEFT);
        if (pressed(focused, 12, y, RAIL_W - 24, 40)) {
            stop_editing();
            g_page = (enum page)i;
        }
    }

    /* player card: the name used in the co-op lobby and the Player Profile */
    float cy = REF_H - 76;
    bool focused = focusable(12, cy, RAIL_W - 24, 60);
    ui_round_rect(12, cy, RAIL_W - 24, 60, 8,
                  hovered(12, cy, RAIL_W - 24, 60) || g_editing_name ? C_RAISE_HI : C_RAISE);
    ui_round_rect(22, cy + 12, 36, 36, 6, C_SNOW);
    char initial[2] = {g_name[0] ? (char)toupper((unsigned char)g_name[0]) : '?', 0};
    label(initial, 40, cy + 37, 18, C_RAIL, UI_CENTER);
    char shown[24];
    snprintf(shown, sizeof(shown), "%s%s", g_name,
             g_editing_name && (SDL_GetTicks() / 450) % 2 ? "_" : "");
    label(shown, 68, cy + 28, 15, C_SNOW, UI_LEFT);
    label(g_editing_name ? TR("Enter para salvar", "Enter to save") : TR("Nome do jogador", "Player name"), 68, cy + 46, 12, C_DIM, UI_LEFT);
    icon(ICON_PENCIL, RAIL_W - 44, cy + 21, 16, C_DIM);
    if (pressed(focused, 12, cy, RAIL_W - 24, 60) && !g_editing_name) {
        g_editing_name = true;
        SDL_StartTextInput();
    } else if (g_click && g_editing_name && !hovered(12, cy, RAIL_W - 24, 60)) {
        stop_editing();
    }
}

/* ---- pages ---- */

static void install_from_apk(void) {
    char apk[1024];
    if (!pick_file(TR("Selecione o APK (original ou BOZ-TV.apk) ou o IPA do iOS", "Choose the APK (original or BOZ-TV.apk) or the iOS IPA"), "*.apk *.ipa", apk, sizeof(apk))) {
        return;
    }
    enum game_mode mode;
    char error[256];
    message(false, TR("Instalando...", "Installing..."));
    if (install_package(apk, g_data_root, &mode, error, sizeof(error))) {
        g_mode = mode;
        save_cfg();
        message(false, mode == MODE_REIMAGINED ? TR("Modo Reimagined instalado.", "Reimagined mode installed.")
                       : mode == MODE_IOS      ? TR("Modo iOS instalado (versão 1.3.5).", "iOS mode installed (version 1.3.5).")
                                               : TR("Modo Original instalado.", "Original mode installed."));
    } else {
        message(true, error);
    }
}

static void choose_obb(void) {
    char obb[1024];
    if (pick_file(TR("Selecione o OBB blackops_etc.dz", "Choose the OBB blackops_etc.dz"), "*.dz", obb, sizeof(obb))) {
        snprintf(g_obb, sizeof(g_obb), "%s", obb);
        save_cfg();
        message(false, TR("OBB selecionado.", "OBB selected."));
    }
}

static void status_row(float y, bool ok, const char *name, const char *detail, const char *action,
                       void (*fn)(void)) {
    icon(ok ? ICON_CHECK : ICON_ALERT, RAIL_W + 40, y + 10, 18, ok ? C_OK : C_GOLD);
    label(name, RAIL_W + 70, y + 22, 15, C_SNOW, UI_LEFT);
    label(detail, RAIL_W + 70, y + 40, 12, C_DIM, UI_LEFT);
    if (button(REF_W - 40 - 150, y + 8, 150, 34, action, false, true)) {
        fn();
    }
    ui_rect(RAIL_W + 40, y + 52, REF_W - RAIL_W - 80, 1, C_LINE);
}

static void draw_play(void) {
    float x = RAIL_W + 40;
    bool installed = install_present(g_data_root, g_mode);
    ui_font(UI_FONT_DISPLAY);
    ui_label(installed && data_ok() ? TR("Pronto para jogar", "Ready to play") : TR("Vamos preparar o jogo", "Let's set up the game"), x, 72, 30,
             C_SNOW, UI_LEFT);
    label(g_mode == MODE_REIMAGINED ? TR("Reimagined  ·  versão da TV  ·  co-op com a TV", "Reimagined  ·  TV version  ·  co-op with the TV")
          : g_mode == MODE_IOS      ? TR("iOS  ·  versão 1.3.5 do iPhone  ·  dados do próprio IPA", "iOS  ·  iPhone version 1.3.5  ·  data from the IPA itself")
                                    : TR("Original  ·  versão do celular  ·  co-op com o celular", "Original  ·  phone version  ·  co-op with phones"),
          x, 98, 13, C_SAND, UI_LEFT);

    static const char *const modes[] = {"Original", "Reimagined", "iOS"};
    int mode = segmented(x, 122, 420, 38, modes, 3, (int)g_mode);
    if (mode != (int)g_mode) {
        g_mode = (enum game_mode)mode;
        save_cfg();
        g_message[0] = 0;
    }
    label(g_mode == MODE_REIMAGINED
              ? TR("Ritmo de horda e drops do BO1, HUD da TV, timer da partida e perfil.", "BO1 horde pace and drops, the TV HUD, match timer and profile.")
          : g_mode == MODE_IOS
              ? TR("A versão do iPhone (1.3.5), sem mudanças. Co-op só com quem tem a mesma versão.", "The iPhone version (1.3.5), unchanged. Co-op only with the same version.")
              : TR("O jogo do celular sem mudanças, para jogar com quem tem o original.", "The phone game unchanged, to play with people who have the original."),
          x, 184, 13, C_DIM, UI_LEFT);

    ui_rect(x, 204, REF_W - RAIL_W - 80, 1, C_LINE);
    char detail[256];
    snprintf(detail, sizeof(detail), "%s",
             installed ? (g_mode == MODE_REIMAGINED ? TR("BOZ-TV.apk instalado", "BOZ-TV.apk installed")
                          : g_mode == MODE_IOS      ? TR("IPA 1.3.5 instalado", "IPA 1.3.5 installed")
                                                    : TR("APK 1.0.11 instalado", "APK 1.0.11 installed"))
                       : (g_mode == MODE_REIMAGINED ? TR("Instale com o BOZ-TV.apk", "Install with BOZ-TV.apk")
                          : g_mode == MODE_IOS      ? TR("Instale com o IPA do iOS", "Install with the iOS IPA")
                                                    : TR("Instale com o APK do Android 1.0.11", "Install with the Android 1.0.11 APK")));
    status_row(205, installed, TR("Jogo", "Game"), detail, installed ? TR("Reinstalar", "Reinstall")
                                                       : g_mode == MODE_IOS ? TR("Instalar IPA", "Install IPA") : TR("Instalar APK", "Install APK"),
               install_from_apk);
    if (g_mode == MODE_IOS) {
        icon(installed ? ICON_CHECK : ICON_ALERT, RAIL_W + 40, 268, 18, installed ? C_OK : C_GOLD);
        label(TR("Dados do jogo", "Game data"), RAIL_W + 70, 280, 15, C_SNOW, UI_LEFT);
        label(installed ? TR("blackops_gles1.dz, copiado do IPA", "blackops_gles1.dz, copied from the IPA")
                        : TR("Vem junto com o IPA (não precisa de OBB)", "Comes with the IPA (no OBB needed)"),
              RAIL_W + 70, 298, 12, C_DIM, UI_LEFT);
        ui_rect(RAIL_W + 40, 310, REF_W - RAIL_W - 80, 1, C_LINE);
        label(TR("No jogo, o cursor aparece sozinho nos menus. Esc pausa e libera o cursor. F11: tela cheia.", "In game the cursor shows by itself in menus. Esc pauses and frees the cursor. F11: fullscreen."), x, 350, 13,
              C_DIM, UI_LEFT);
        return;
    }
    const char *base = strrchr(g_obb, '/');
    snprintf(detail, sizeof(detail), "%s",
             obb_ok() ? (base ? base + 1 : g_obb) : TR("Arquivo blackops_etc.dz (não é copiado)", "The blackops_etc.dz file (it is not copied)"));
    status_row(258, obb_ok(), TR("Dados do jogo (OBB)", "Game data (OBB)"), detail, obb_ok() ? TR("Alterar", "Change") : TR("Selecionar", "Choose"),
               choose_obb);

    label(TR("No jogo, o cursor aparece sozinho nos menus. Esc pausa e libera o cursor. F11: tela cheia.", "In game the cursor shows by itself in menus. Esc pauses and frees the cursor. F11: fullscreen."), x, 350, 13,
          C_DIM, UI_LEFT);
}

static void draw_settings(void) {
    float x = RAIL_W + 40, cx = REF_W - 40;
    ui_font(UI_FONT_DISPLAY);
    ui_label(TR("Configurações", "Settings"), x, 72, 30, C_SNOW, UI_LEFT);
    bool changed = false;
    float y = 86;

    row(y, TR("Tela", "Display"), TR("Tela cheia usa a resolução do monitor, sem bordas", "Fullscreen uses the monitor's resolution, borderless"));
    const char *const screens[] = {TR("Tela cheia", "Fullscreen"), TR("Janela", "Window")};
    int fs = segmented(cx - 240, y + 6, 240, 32, screens, 2, g_opt.fullscreen ? 0 : 1);
    changed |= (fs == 0) != g_opt.fullscreen;
    g_opt.fullscreen = fs == 0;
    y += 44;

    row(y, TR("Sincronia vertical", "Vertical sync"), TR("Sem tearing; usa a taxa do monitor", "No tearing; follows the monitor's refresh rate"));
    bool vs = toggle(cx - 44, y + 11, g_opt.vsync);
    changed |= vs != g_opt.vsync;
    g_opt.vsync = vs;
    y += 44;

    row(y, TR("Limite de FPS", "FPS limit"),
        TR("30 = ritmo original do jogo", "30 = the game's original pacing"));
    static const int limits[] = {30, 60, 120, 144, 0};
    const char *const limit_names[] = {"30", "60", "120", "144", TR("Sem limite", "Unlimited")};
    int cur = 4;
    for (int i = 0; i < 5; ++i) {
        if (g_opt.fps_limit == limits[i]) {
            cur = i;
        }
    }
    int lim = segmented(cx - 400, y + 6, 400, 32, limit_names, 5, cur);
    changed |= limits[lim] != g_opt.fps_limit;
    g_opt.fps_limit = limits[lim];
    y += 44;

    row(y, TR("Contador de FPS do PC", "PC FPS counter"), TR("F3 alterna durante o jogo", "F3 toggles it in game"));
    bool sf = toggle(cx - 44, y + 11, g_opt.show_fps);
    changed |= sf != g_opt.show_fps;
    g_opt.show_fps = sf;
    y += 44;

    row(y, TR("Mira com o mouse", "Mouse aim"),
        TR("Arrastar: 1:1, como um dedo  ·  Analógico: como um controle",
           "Drag: 1:1, like a finger  ·  Analog: like a controller"));
    const char *const looks[] = {TR("Arrastar", "Drag"), TR("Analógico", "Analog")};
    int ml = segmented(cx - 240, y + 6, 240, 32, looks, 2, g_opt.mouse_drag ? 0 : 1);
    changed |= (ml == 0) != g_opt.mouse_drag;
    g_opt.mouse_drag = ml == 0;
    y += 44;

    row(y, TR("Idioma do launcher", "Launcher language"), NULL);
    static const char *const langs[] = {"Português", "English"};
    int lang = segmented(cx - 240, y + 6, 240, 32, langs, 2, g_opt.english ? 1 : 0);
    changed |= (lang == 1) != g_opt.english;
    g_opt.english = lang == 1;
    y += 44;

    char value[48];
    snprintf(value, sizeof(value), TR("Sensibilidade do mouse: %.2f", "Mouse sensitivity: %.2f"), g_opt.mouse_sensitivity);
    row(y, value, NULL);
    float sens = slider(cx - 240, y + 20, 240, g_opt.mouse_sensitivity, 0.2f, 3.0f);
    sens = (float)(int)(sens * 20.0f + 0.5f) / 20.0f;
    changed |= sens != g_opt.mouse_sensitivity;
    g_opt.mouse_sensitivity = sens;
    y += 44;

    row(y, TR("Inverter eixo Y", "Invert Y axis"), NULL);
    bool iy = toggle(cx - 44, y + 11, g_opt.invert_y);
    changed |= iy != g_opt.invert_y;
    g_opt.invert_y = iy;
    y += 48;

    if (button(x, y, 180, 36, TR("Restaurar padrões", "Reset to defaults"), false, true)) {
        g_opt.fullscreen = true;
        g_opt.vsync = true;
        g_opt.fps_limit = 0;
        g_opt.show_fps = false;
        g_opt.mouse_drag = false;
        g_opt.mouse_sensitivity = 1.0f;
        g_opt.invert_y = false;
        changed = true;
    }
    if (changed) {
        settings_save();
    }
}

static void save_bindings(void) {
    char path[1200];
    cfg_path(path, sizeof(path), "bindings.txt");
    bindings_save(path);
}

static void draw_controls(void) {
    float x = RAIL_W + 40;
    ui_font(UI_FONT_DISPLAY);
    ui_label(TR("Controles", "Controls"), x, 62, 30, C_SNOW, UI_LEFT);
    label(g_capture_action >= 0 ? TR("Pressione uma tecla, botão do mouse ou do controle  ·  Del apaga  ·  Esc cancela", "Press a key, mouse or controller button  ·  Del clears  ·  Esc cancels")
                                : TR("Clique numa caixa para trocar. Cada ação aceita até 3 entradas.", "Click a box to change it. Each action takes up to 3 inputs."),
          x, 84, 13, g_capture_action >= 0 ? C_GOLD : C_DIM, UI_LEFT);
    for (int a = 0; a < BIND_COUNT; ++a) {
        float y = 88 + (float)a * 24.0f; /* all actions + reset fit above the footer */
        label(bindings_label((enum bind_action)a), x, y + 16, 13, C_SAND, UI_LEFT);
        for (int s = 0; s < 3; ++s) {
            float bx = REF_W - 40 - (float)(3 - s) * 136;
            bool capturing = g_capture_action == a && g_capture_slot == s;
            bool focused = focusable(bx, y, 130, 22);
            bool hover = hovered(bx, y, 130, 22);
            ui_round_rect(bx, y, 130, 22, 6, capturing ? C_ACCENT : hover ? C_RAISE_HI : C_RAISE);
            char name[48];
            bindings_describe(bindings_get((enum bind_action)a, s), name, sizeof(name));
            label(capturing ? "..." : name, bx + 65, y + 16, 12,
                  bindings_get((enum bind_action)a, s)[0] ? C_SNOW : C_DIM, UI_CENTER);
            if (g_capture_action < 0 && pressed(focused, bx, y, 130, 22)) {
                g_capture_action = a;
                g_capture_slot = s;
                g_click = false;
                g_activate = false;
            }
        }
    }
    if (g_capture_action < 0 && button(x, 88 + BIND_COUNT * 24.0f + 4, 170, 28, TR("Restaurar padrão", "Reset to defaults"), false,
                                       true)) {
        bindings_reset();
        save_bindings();
        message(false, TR("Controles restaurados.", "Controls reset."));
    }
}

static void draw_footer(void) {
    float y = REF_H - FOOT_H;
    ui_rect(RAIL_W, y, REF_W - RAIL_W, FOOT_H, C_GROUND);
    ui_rect(RAIL_W, y, REF_W - RAIL_W, 1, C_LINE);
    bool installed = install_present(g_data_root, g_mode);
    const char *primary = !installed ? (g_mode == MODE_IOS ? TR("Instalar IPA", "Install IPA") : TR("Instalar APK", "Install APK"))
                          : !data_ok() ? TR("Selecionar OBB", "Choose OBB") : TR("JOGAR", "PLAY");
    if (g_message[0]) {
        icon(g_message_bad ? ICON_ALERT : ICON_CHECK, RAIL_W + 24, y + 23, 16,
             g_message_bad ? C_BAD : C_OK);
        label(g_message, RAIL_W + 48, y + 37, 13, g_message_bad ? C_BAD : C_SAND, UI_LEFT);
    } else {
        label(g_mode == MODE_REIMAGINED ? TR("Modo Reimagined", "Reimagined mode")
              : g_mode == MODE_IOS      ? TR("Modo iOS", "iOS mode")
                                        : TR("Modo Original", "Original mode"), RAIL_W + 24, y + 37,
              13, C_DIM, UI_LEFT);
    }
    if (button(REF_W - 24 - 170, y + 12, 170, 40, primary, true, true)) {
        stop_editing();
        if (!installed) {
            install_from_apk();
        } else if (!data_ok()) {
            choose_obb();
        } else {
            g_page = PAGE_START;
        }
    }
}

/* ---- events ---- */

static void finish_capture(const char *input) {
    if (input) {
        bindings_set((enum bind_action)g_capture_action, g_capture_slot, input);
        save_bindings();
    }
    g_capture_action = -1;
}

static void handle(const SDL_Event *ev) {
    char input[48];
    switch (ev->type) {
    case SDL_QUIT:
        exit(0);
    case SDL_MOUSEMOTION:
        ui_window_to_ref(ev->motion.x, ev->motion.y, &g_mx, &g_my);
        if (abs(ev->motion.xrel) + abs(ev->motion.yrel) > 2) {
            g_kbd_mode = false;
        }
        break;
    case SDL_MOUSEBUTTONDOWN:
        ui_window_to_ref(ev->button.x, ev->button.y, &g_mx, &g_my);
        if (g_capture_action >= 0) {
            snprintf(input, sizeof(input), "MOUSE:%d", ev->button.button);
            finish_capture(input);
        } else if (ev->button.button == SDL_BUTTON_LEFT) {
            g_click = true;
            g_mouse_held = true;
            g_kbd_mode = false;
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (ev->button.button == SDL_BUTTON_LEFT) {
            g_mouse_held = false;
        }
        break;
    case SDL_MOUSEWHEEL:
        if (g_capture_action >= 0 && ev->wheel.y) {
            finish_capture(ev->wheel.y > 0 ? "WHEEL:UP" : "WHEEL:DOWN");
        }
        break;
    case SDL_CONTROLLERBUTTONDOWN: {
        SDL_GameControllerButton b = (SDL_GameControllerButton)ev->cbutton.button;
        if (g_capture_action >= 0) {
            snprintf(input, sizeof(input), "PAD:%s", SDL_GameControllerGetStringForButton(b));
            finish_capture(input);
            break;
        }
        g_kbd_mode = true;
        if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) {
            g_focus_move = -1;
        } else if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
            g_focus_move = 1;
        } else if (b == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
            g_left = true;
            g_focus_move = -1;
        } else if (b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            g_right = true;
            g_focus_move = 1;
        } else if (b == SDL_CONTROLLER_BUTTON_A) {
            g_activate = true;
        } else if (b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
            g_page = (enum page)((g_page + PAGE_COUNT - 1) % PAGE_COUNT);
        } else if (b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
            g_page = (enum page)((g_page + 1) % PAGE_COUNT);
        } else if (b == SDL_CONTROLLER_BUTTON_START && install_present(g_data_root, g_mode) &&
                   data_ok()) {
            g_page = PAGE_START;
        }
        break;
    }
    case SDL_CONTROLLERAXISMOTION:
        if (g_capture_action >= 0 && ev->caxis.value > 20000 &&
            (ev->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
             ev->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
            snprintf(input, sizeof(input), "PADAXIS:%s",
                     SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)ev->caxis.axis));
            finish_capture(input);
        }
        break;
    case SDL_CONTROLLERDEVICEADDED:
        SDL_GameControllerOpen(ev->cdevice.which);
        break;
    case SDL_TEXTINPUT:
        if (g_editing_name && strlen(g_name) + strlen(ev->text.text) < 14) {
            strcat(g_name, ev->text.text);
        }
        break;
    case SDL_KEYDOWN: {
        SDL_Scancode sc = ev->key.keysym.scancode;
        if (g_capture_action >= 0) {
            if (sc == SDL_SCANCODE_ESCAPE) {
                finish_capture(NULL);
            } else if (sc == SDL_SCANCODE_DELETE || sc == SDL_SCANCODE_BACKSPACE) {
                finish_capture("");
            } else {
                snprintf(input, sizeof(input), "KEY:%s", SDL_GetScancodeName(sc));
                finish_capture(input);
            }
            break;
        }
        if (g_editing_name) {
            if (sc == SDL_SCANCODE_BACKSPACE && g_name[0]) {
                g_name[strlen(g_name) - 1] = 0;
            } else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_ESCAPE) {
                stop_editing();
            }
            break;
        }
        g_kbd_mode = true;
        if (sc == SDL_SCANCODE_UP || (sc == SDL_SCANCODE_TAB && (ev->key.keysym.mod & KMOD_SHIFT))) {
            g_focus_move = -1;
        } else if (sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_TAB) {
            g_focus_move = 1;
        } else if (sc == SDL_SCANCODE_LEFT) {
            g_left = true;
        } else if (sc == SDL_SCANCODE_RIGHT) {
            g_right = true;
        } else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_SPACE) {
            g_activate = true;
        }
        break;
    }
    default:
        break;
    }
}

enum game_mode launcher_run(char *obb, size_t obb_size) {
    load_cfg();
    snprintf(g_name, sizeof(g_name), "%s", host_player_name());
    char path[1200];
    snprintf(path, sizeof(path), "%s/reimagined/assets/character-menu/comic.ttf", g_data_root);
    ui_load_font_slot(UI_FONT_DISPLAY, path);
    snprintf(path, sizeof(path), "%s/reimagined/assets/character-menu/carbon.ttf", g_data_root);
    ui_load_font_slot(UI_FONT_BODY, path);
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    SDL_SetWindowFullscreen(video_window(), 0);
    SDL_SetWindowSize(video_window(), REF_W, REF_H);
    SDL_SetWindowPosition(video_window(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SetWindowTitle(video_window(), "Black Ops Zombies - PC");
    ui_set_reference(REF_W, REF_H);

    void (*clear_color)(float, float, float, float) = gl_proc("glClearColor");
    void (*clear)(uint32_t) = gl_proc("glClear");
    g_page = getenv("CODBOZ_LAUNCHER_PAGE") ? (enum page)(atoi(getenv("CODBOZ_LAUNCHER_PAGE")) % PAGE_COUNT)
                                            : PAGE_PLAY;
    g_focus = 0;
    while (g_page != PAGE_START) {
        g_click = false;
        g_activate = false;
        g_left = g_right = false;
        g_focus_move = 0;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            handle(&ev);
        }
        if (g_focus_move && g_focus_count) {
            g_focus = (g_focus + g_focus_move + g_focus_count) % g_focus_count;
        }
        int w, h;
        SDL_GL_GetDrawableSize(video_window(), &w, &h);
        clear_color(0.106f, 0.086f, 0.082f, 1);
        clear(0x4000);
        ui_begin(w, h);
        g_focus_count = 0;
        ui_rect(0, 0, REF_W, REF_H, C_GROUND);
        draw_rail();
        if (g_page == PAGE_PLAY) {
            draw_play();
        } else if (g_page == PAGE_SETTINGS) {
            draw_settings();
        } else if (g_page == PAGE_CONTROLS) {
            draw_controls();
        }
        if (g_page != PAGE_START) {
            draw_footer();
        }
        ui_end();
        extern void video_debug_capture(void);
        video_debug_capture();
        SDL_GL_SwapWindow(video_window());
        SDL_Delay(8);
    }
    stop_editing();
    save_cfg();
    ui_set_reference(1280, 720);
    ui_font(UI_FONT_DISPLAY);
    SDL_SetWindowTitle(video_window(), "Call of Duty: Black Ops Zombies");
    snprintf(obb, obb_size, "%s", g_obb);
    return g_mode;
}
