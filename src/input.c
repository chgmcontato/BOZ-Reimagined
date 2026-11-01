/*
 * Input. The game runs with the Sony Xperia Play profile: physical buttons arrive
 * as Xperia key codes and the two analog touchpads drive movement and look, so no
 * virtual sticks are drawn. On the PC:
 *   - menus: the real mouse is the touch pointer;
 *   - in game (mouse captured): WASD -> left touchpad, mouse motion -> right
 *     touchpad, buttons -> Xperia keys;
 *   - a game controller works in both modes, as in the handheld port.
 */
#include "bindings.h"
#include "host.h"
#include "menus.h"
#include "reimagined.h"
#include "screens.h"
#include "ui.h"

#include <SDL.h>
#include <math.h>
#include <stdlib.h>

enum {
    KEY_STATE_DOWN = 1,
    KEY_STATE_PRESSED = 2,
    KEY_STATE_RELEASED = 4,
    POINTER_STATE_UP = 0,
    POINTER_STATE_DOWN = 1,
    POINTER_STATE_PRESSED = 2,
    POINTER_STATE_RELEASED = 4,
    KEYBOARD_KEY_COUNT = 256,
    TOUCHPAD_COUNT = 2,
    TOUCHPAD_WIDTH = 960,
    TOUCHPAD_HEIGHT = 544,
    AXIS_DEADZONE = 7000,
    MENU_CLICK_COOLDOWN_MS = 200,
    MENU_TOUCH_SETTLE_MS = 32,
    MENU_TOUCH_HOLD_MS = 200,

    /* with the boz-reimagined bindings (see assets.c) */
    XPERIA_KEY_RELOAD_PATCHED = 9,
    XPERIA_KEY_LEAVE_AIM = 15,
    XPERIA_KEY_CLAYMORE = 16,
    XPERIA_KEY_TACTICAL_GRENADE = 10,
    XPERIA_KEY_CHANGE_WEAPON = 11,
    XPERIA_KEY_CROUCH_PRONE = 12,
    XPERIA_KEY_PAUSE = 72,
    XPERIA_KEY_AIM = 74,
    XPERIA_KEY_SHOOT = 75,
    XPERIA_KEY_ACTION_SPRINT = 78,
    XPERIA_KEY_MELEE = 89,
    XPERIA_KEY_THROW_GRENADE = 90,
    XPERIA_KEY_RELOAD = 126,
    S3E_KEY_BACK = 0x81, /* s3eKeyBack */
    S3E_KEY_ABS_GAME_A = 200,
    S3E_KEY_ABS_BSK = 210,
};

/* Xperia key sent for each bindable game action (movement goes to the left touchpad). */
static const uint32_t k_action_key[BIND_COUNT] = {
    [BIND_SHOOT] = XPERIA_KEY_SHOOT,
    [BIND_AIM] = XPERIA_KEY_AIM,
    [BIND_RELOAD] = XPERIA_KEY_RELOAD_PATCHED,
    [BIND_USE] = XPERIA_KEY_ACTION_SPRINT,
    [BIND_MELEE] = XPERIA_KEY_MELEE,
    [BIND_GRENADE] = XPERIA_KEY_THROW_GRENADE,
    [BIND_TACTICAL] = XPERIA_KEY_TACTICAL_GRENADE,
    [BIND_CLAYMORE] = XPERIA_KEY_CLAYMORE,
    [BIND_CROUCH] = XPERIA_KEY_CROUCH_PRONE,
    [BIND_SWITCH] = XPERIA_KEY_CHANGE_WEAPON,
    [BIND_PAUSE] = XPERIA_KEY_PAUSE,
    [BIND_NUKE] = XPERIA_KEY_SHOOT, /* the Dead Ops special weapon listens to the shoot key */
    [BIND_BOOST] = XPERIA_KEY_AIM,  /* and its speed boost to the aim key */
};

struct callback_slot {
    uint32_t id;
    uint32_t callback;
    uint32_t user_data;
};

static struct callback_slot g_keyboard_cb[16];
static struct callback_slot g_pointer_cb[8];
static struct callback_slot g_touchpad_cb[8];

static uint8_t g_key_live[KEYBOARD_KEY_COUNT];
static uint8_t g_key_published[KEYBOARD_KEY_COUNT];

static int32_t g_pointer_x, g_pointer_y;
static int g_pointer_down;
static uint8_t g_pointer_state;
static uint8_t g_pointer_published;
static uint64_t g_pointer_updates, g_pointer_down_at;
static bool g_pointer_uses_polling, g_pointer_press_seen;
static bool g_mouse_touch_active, g_mouse_release_pending;
static bool g_menu_mouse_owned;
static bool g_menu_mouse_down;
static uint64_t g_menu_next_click_at;
static bool g_input_debug;

static bool menu_mouse_begin(void) {
    if (g_menu_mouse_down) {
        return false;
    }
    g_menu_mouse_down = true;
    uint64_t now = monotonic_ms();
    if (now < g_menu_next_click_at) {
        g_menu_mouse_owned = true; /* Consume this rejected click's mouse-up too. */
        if (g_input_debug) log_info("[debug] input menu: click ignored (200 ms cooldown)");
        return false;
    }
    g_menu_next_click_at = now + MENU_CLICK_COOLDOWN_MS;
    return true;
}

static bool menu_mouse_owns_release(void) {
    bool owned = g_menu_mouse_owned;
    g_menu_mouse_owned = false;
    return owned;
}

static int g_pad_active[TOUCHPAD_COUNT];
static int32_t g_pad_x[TOUCHPAD_COUNT], g_pad_y[TOUCHPAD_COUNT];

static bool g_captured; /* gameplay mode: mouse drives the camera */
static int g_mouse_dx, g_mouse_dy;
static float g_look_x, g_look_y;
static int g_pumping;
static bool g_look_frame_start = true; /* first pump since the last swap */
static float g_look_dt;
static bool g_manual;         /* Tab/Esc override until the game changes mode again */
static uint64_t g_pause_release_at;
static int g_auto_capture = -1; /* last automatic decision */

static SDL_GameController *g_controller;

/*
 * Test automation: CODBOZ_SCRIPT="TIME CMD ARGS; ..." with TIME in seconds since start.
 *   click X Y | mclick X Y | key CODE 0/1 | move X Y | look DX DY | capture 0/1 | esc | shot
 *   "@screen" waits for a recognised screen; "shot" saves the frame to $CODBOZ_SHOT_DIR.
 */
struct script_step {
    float time;
    char cmd[16];
    float a, b;
};
static struct script_step g_script[256];
static int g_script_count, g_script_next;
static uint64_t g_script_start;
static float g_script_move_x, g_script_move_y;
static uint64_t g_script_release_at;

static uint32_t g_event_buffer;
static uint32_t g_event_slot;

SDL_Window *video_window(void);

static uint32_t event_buffer(void) {
    if (!g_event_buffer) {
        g_event_buffer = H2G(arena_alloc(64 * 32));
    }
    g_event_slot = (g_event_slot + 1) & 63;
    return g_event_buffer + g_event_slot * 32;
}

static void dispatch(struct callback_slot *slots, size_t count, uint32_t id, uint32_t event) {
    for (size_t i = 0; i < count; ++i) {
        if (slots[i].callback && slots[i].id == id) {
            guest_call2(slots[i].callback, event, slots[i].user_data);
        }
    }
}

static void slot_register(struct callback_slot *slots, size_t count) {
    uint32_t id = arg(0), cb = arg(1), ud = arg(2);
    for (size_t i = 0; i < count; ++i) {
        if (slots[i].callback == cb && slots[i].id == id) {
            slots[i].user_data = ud;
            ret(0);
            return;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (!slots[i].callback) {
            slots[i] = (struct callback_slot){id, cb, ud};
            break;
        }
    }
    ret(0);
}

static void slot_unregister(struct callback_slot *slots, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (slots[i].id == arg(0) && (!arg(1) || slots[i].callback == arg(1))) {
            memset(&slots[i], 0, sizeof(slots[i]));
        }
    }
    ret(0);
}

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* ---- keyboard (Xperia keys) ---- */

static void key_set(uint32_t key, int down) {
    if (key >= KEYBOARD_KEY_COUNT || ((g_key_live[key] & KEY_STATE_DOWN) != 0) == (down != 0)) {
        return;
    }
    if (down) {
        g_key_live[key] |= KEY_STATE_DOWN | KEY_STATE_PRESSED;
    } else {
        g_key_live[key] &= (uint8_t)~KEY_STATE_DOWN;
        g_key_live[key] |= KEY_STATE_RELEASED;
    }
    uint32_t event = event_buffer();
    int32_t *e = G2H(event);
    e[0] = (int32_t)key;
    e[1] = down ? 1 : 0;
    dispatch(g_keyboard_cb, COUNT(g_keyboard_cb), 0, event);
}

static void release_all_keys(void) {
    for (uint32_t key = 0; key < KEYBOARD_KEY_COUNT; ++key) {
        key_set(key, 0);
    }
}

/* ---- pointer (touch) ---- */

static void pointer_button(int down) {
    if (down == g_pointer_down) {
        return;
    }
    g_pointer_down = down;
    g_pointer_state = down ? POINTER_STATE_DOWN | POINTER_STATE_PRESSED : POINTER_STATE_RELEASED;
    g_pointer_press_seen = false;
    if (down) g_pointer_down_at = monotonic_ms();
    if (g_input_debug) {
        log_info("[debug] input menu: %s at %d,%d t=%llu", down ? "down" : "up",
                 g_pointer_x, g_pointer_y, (unsigned long long)monotonic_ms());
    }
    uint32_t event = event_buffer();
    int32_t *e = G2H(event);
    e[0] = 0; /* button / touch id */
    e[1] = down;
    e[2] = g_pointer_x;
    e[3] = g_pointer_y;
    dispatch(g_pointer_cb, COUNT(g_pointer_cb), 0, event); /* S3E_POINTER_BUTTON_EVENT */
    dispatch(g_pointer_cb, COUNT(g_pointer_cb), 2, event); /* S3E_POINTER_TOUCH_EVENT */
}

static void pointer_motion(int32_t x, int32_t y) {
    if (x == g_pointer_x && y == g_pointer_y) {
        return;
    }
    g_pointer_x = x;
    g_pointer_y = y;
    uint32_t event = event_buffer();
    int32_t *e = G2H(event);
    e[0] = x;
    e[1] = y;
    dispatch(g_pointer_cb, COUNT(g_pointer_cb), 1, event); /* MOTION */
    event = event_buffer();
    e = G2H(event);
    e[0] = 0;
    e[1] = x;
    e[2] = y;
    dispatch(g_pointer_cb, COUNT(g_pointer_cb), 3, event); /* TOUCH_MOTION */
}

/* Publish edges after pumping, just like the keyboard. Other host pumps must not
 * consume PRESSED/RELEASED before the guest's s3ePointerUpdate/GetState pair. */
static void pointer_publish(void) {
    g_pointer_uses_polling = true;
    ++g_pointer_updates;
    g_pointer_published = g_pointer_state;
    if (g_pointer_down) g_pointer_press_seen = true;
    g_pointer_state = g_pointer_down ? POINTER_STATE_DOWN : POINTER_STATE_UP;
}

static bool pointer_can_release(uint64_t now) {
    return now >= g_pointer_down_at + MENU_TOUCH_HOLD_MS &&
           (g_pointer_press_seen || !g_pointer_uses_polling);
}

static void mouse_touch_update(void) {
    if (g_mouse_release_pending && pointer_can_release(monotonic_ms())) {
        g_mouse_release_pending = g_mouse_touch_active = false;
        pointer_button(0);
    }
}

/* Scripted taps on the Flash menu (the reimagined menus press its buttons). */
static uint64_t g_tap_release_at; /* non-zero while a tap is in progress */
static uint64_t g_tap_press_at;   /* the finger lands this long after moving there */
static uint64_t g_tap_position_update;

bool input_has_controller(void) {
    return g_controller != NULL;
}

void input_cancel_tap(void) {
    g_tap_press_at = g_tap_release_at = 0;
    g_mouse_release_pending = g_mouse_touch_active = false;
    pointer_button(0);
}

bool input_tap_busy(void) {
    return g_tap_release_at != 0;
}

static bool menu_mouse_touch_busy(void) {
    /* A new raw DOWN must not overwrite an UP the guest has not published yet. */
    return g_mouse_touch_active || input_tap_busy() || g_pointer_down ||
           (g_pointer_uses_polling && (g_pointer_state & POINTER_STATE_RELEASED));
}

/*
 * Move, settle in real time, then hold for at least 200 ms. For polling clients,
 * wait for the guest to publish the position/down before advancing the gesture;
 * this is an input acknowledgement, independent of the rendering frame rate.
 */
void input_tap(float x, float y) {
    if (g_tap_release_at || g_mouse_touch_active) {
        return;
    }
    pointer_motion((int32_t)(x * (float)video_width() / 1280.0f),
                   (int32_t)(y * (float)video_height() / 720.0f));
    uint64_t now = monotonic_ms();
    g_tap_position_update = g_pointer_updates;
    g_tap_press_at = now + MENU_TOUCH_SETTLE_MS;
    g_tap_release_at = g_tap_press_at + MENU_TOUCH_HOLD_MS;
}

static void tap_update(void) {
    uint64_t now = monotonic_ms();
    if (g_tap_press_at && now >= g_tap_press_at &&
        (!g_pointer_uses_polling || g_pointer_updates != g_tap_position_update)) {
        g_tap_press_at = 0;
        pointer_button(1);
        g_tap_release_at = now + MENU_TOUCH_HOLD_MS;
    }
    if (g_tap_release_at && !g_tap_press_at && now >= g_tap_release_at &&
        pointer_can_release(now)) {
        g_tap_release_at = 0;
        pointer_button(0);
    }
}

/* ---- Xperia touchpads ---- */

static void pad_button(uint32_t id, int pressed) {
    uint32_t event = event_buffer();
    int32_t *e = G2H(event);
    e[0] = (int32_t)id;
    e[1] = pressed;
    e[2] = g_pad_x[id];
    e[3] = g_pad_y[id];
    dispatch(g_touchpad_cb, COUNT(g_touchpad_cb), 0, event);
}

static void pad_motion(uint32_t id) {
    uint32_t event = event_buffer();
    int32_t *e = G2H(event);
    e[0] = (int32_t)id;
    e[1] = g_pad_x[id];
    e[2] = g_pad_y[id];
    dispatch(g_touchpad_cb, COUNT(g_touchpad_cb), 1, event);
}

static void pad_release(uint32_t id) {
    if (g_pad_active[id]) {
        g_pad_active[id] = 0;
        pad_button(id, 0);
    }
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* Deflection in [-1, 1] mapped around the pad's virtual stick centre. */
static void pad_stick(uint32_t id, float x, float y, int32_t cx, int32_t cy, int32_t rx,
                      int32_t ry) {
    if (fabsf(x) < 0.001f && fabsf(y) < 0.001f) {
        pad_release(id);
        return;
    }
    int32_t px = clampi(cx + (int32_t)lrintf(x * (float)rx), 0, TOUCHPAD_WIDTH - 1);
    int32_t py = clampi(cy + (int32_t)lrintf(y * (float)ry), 0, TOUCHPAD_HEIGHT - 1);
    if (!g_pad_active[id]) {
        g_pad_active[id] = 1;
        g_pad_x[id] = px;
        g_pad_y[id] = py;
        pad_motion(id);
        pad_button(id, 1);
        return;
    }
    if (px != g_pad_x[id] || py != g_pad_y[id]) {
        g_pad_x[id] = px;
        g_pad_y[id] = py;
        pad_motion(id);
    }
}

/* ---- mode switching ---- */

/*
 * "Drag" mouse look: a finger held on the Xperia look touchpad follows the mouse 1:1,
 * and is lifted and put back in the middle of the pad near its edges.
 */
static bool g_drag_down;
static float g_drag_x, g_drag_y;

static void drag_place(void) {
    g_drag_x = TOUCHPAD_WIDTH * 4 / 5;
    g_drag_y = TOUCHPAD_HEIGHT / 2;
    g_pad_x[1] = (int32_t)g_drag_x;
    g_pad_y[1] = (int32_t)g_drag_y;
    g_pad_active[1] = 1;
    pad_motion(1);
    pad_button(1, 1);
    g_drag_down = true;
}

static void drag_release(void) {
    if (g_drag_down) {
        pad_release(1);
        g_drag_down = false;
    }
}

static void drag_update(int dx, int dy) {
    if (!g_drag_down) {
        drag_place();
    }
    if (!dx && !dy) {
        return;
    }
    float k = 0.8f * g_opt.mouse_sensitivity; /* touchpad pixels per mouse count */
    g_drag_x += (float)dx * k;
    g_drag_y += (float)dy * k * (g_opt.invert_y ? -1.0f : 1.0f);
    if (g_drag_x < TOUCHPAD_WIDTH * 0.62f || g_drag_x > TOUCHPAD_WIDTH * 0.98f ||
        g_drag_y < TOUCHPAD_HEIGHT * 0.08f || g_drag_y > TOUCHPAD_HEIGHT * 0.92f) {
        drag_release();
        drag_place();
        return;
    }
    g_pad_x[1] = (int32_t)lrintf(g_drag_x);
    g_pad_y[1] = (int32_t)lrintf(g_drag_y);
    pad_motion(1);
}

static void set_captured(bool captured) {
    if (captured == g_captured) {
        return;
    }
    drag_release();
    g_captured = captured;
    log_info("mouse %s", captured ? "captured (camera)" : "released (cursor)");
    SDL_SetRelativeMouseMode(captured ? SDL_TRUE : SDL_FALSE);
    g_mouse_dx = g_mouse_dy = 0;
    g_look_x = g_look_y = 0;
    if (captured) {
        input_cancel_tap();
    } else {
        pad_release(0);
        pad_release(1);
        release_all_keys();
    }
    log_debug("mouse %s", captured ? "captured" : "released");
}

/* Window pixels -> game surface coordinates (they match unless HiDPI scaling). */
static void window_to_surface(int wx, int wy, int32_t *sx, int32_t *sy) {
    /* the game frame is letterboxed in the window at the reference 1280x720 */
    float rx, ry;
    ui_window_to_ref(wx, wy, &rx, &ry);
    *sx = clampi((int32_t)(rx * (float)video_width() / 1280.0f), 0, video_width() - 1);
    *sy = clampi((int32_t)(ry * (float)video_height() / 720.0f), 0, video_height() - 1);
}

/* Screens where the next unknown frame is gameplay (or its loading). */
static bool leads_to_game(const char *id) {
    static const char *const ids[] = {"charselect", "single", "newgame", "coopmap", "cooplobby",
                                      "pause", "pausesettings", "sensitivity", "savequit"};
    for (size_t i = 0; id && i < sizeof(ids) / sizeof(ids[0]); ++i) {
        if (strcmp(id, ids[i]) == 0) {
            return true;
        }
    }
    return false;
}

/*
 * Does the game want the mouse as a camera? Menus are recognised from the frame
 * (screens.c); the reimagined binary also tells when a match is running.
 */
/*
 * The iOS build's in-match pause matches no recognition probe (they come from TV frames), so it
 * looks like gameplay and a click on RETURN would only grab the mouse. Remember the Esc instead.
 */
static bool g_native_pause_mode, g_native_pause_open;
static uint64_t g_native_pause_close_at;

void input_set_native_pause_mode(bool enabled) {
    g_native_pause_mode = enabled;
}

/* Let the tap on RETURN reach the game before the camera takes the mouse back. */
static void native_pause_close(void) {
    g_native_pause_close_at = monotonic_ms() + 400;
}

static bool native_pause_active(void) {
    if (!g_native_pause_open) {
        return false;
    }
    if (screens_current() ||
        (g_native_pause_close_at && monotonic_ms() >= g_native_pause_close_at)) {
        g_native_pause_open = false;
        g_native_pause_close_at = 0;
        return false;
    }
    return true;
}

static int wants_capture(void) {
    if (native_pause_active()) {
        return 0;
    }
    if (screens_available()) {
        if (screens_current()) {
            return 0;
        }
        if (reimagined_patched()) {
            return reimagined_match_active();
        }
        return leads_to_game(screens_last_known()) &&
               monotonic_ms() - screens_changed_ms() > 1000;
    }
    if (reimagined_patched()) {
        return reimagined_match_active();
    }
    return -1; /* no way to know: Tab decides */
}

static void handle_event(const SDL_Event *ev) {
    switch (ev->type) {
    case SDL_QUIT:
        log_info("window closed");
        device_request_quit();
        audio_shutdown();
        video_shutdown();
        exit(0);
    case SDL_WINDOWEVENT:
        if (ev->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            input_cancel_tap();
            g_menu_mouse_down = g_menu_mouse_owned = false;
            set_captured(false);
        }
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        int down = ev->type == SDL_KEYDOWN;
        SDL_Scancode sc = ev->key.keysym.scancode;
        if (down && ev->key.repeat) {
            break;
        }
        if (sc == SDL_SCANCODE_TAB && down) {
            /* during a Reimagined match Tab belongs to the scoreboard while it is bound to it */
            if (!(g_captured && reimagined_scoreboard_available() &&
                  bindings_uses(BIND_SCOREBOARD, "KEY:Tab"))) {
                g_manual = true;
                set_captured(!g_captured);
            }
            break;
        }
        if (sc == SDL_SCANCODE_F11 && down) {
            uint32_t flags = SDL_GetWindowFlags(video_window());
            SDL_SetWindowFullscreen(video_window(), (flags & SDL_WINDOW_FULLSCREEN_DESKTOP)
                                                        ? 0
                                                        : SDL_WINDOW_FULLSCREEN_DESKTOP);
            break;
        }
        if (sc == SDL_SCANCODE_F3 && down) {
            g_opt.show_fps = !g_opt.show_fps;
            break;
        }
        if (sc == SDL_SCANCODE_T && down && screens_is("pause")) {
            reimagined_toggle_timer();
            break;
        }
        if (menus_editing_name()) {
            if (down) {
                menus_name_key(sc);
            }
            break;
        }
        /*
         * Escape in gameplay: pause the game and free the cursor. Screens without a pause
         * (the original build's revive offer after dying) still get a usable cursor.
         */
        if (sc == SDL_SCANCODE_ESCAPE && g_captured && down) {
            bool deadops = reimagined_deadops();
            g_manual = true;
            set_captured(false);
            if (deadops) {
                /* Dead Ops ignores the pause key: press its on-screen "||" button (top right) */
                input_tap(1232, 44);
                {
                    extern void video_request_shot(const char *path);
                    video_request_shot("build/doa-pause.ppm"); /* to locate the "||" button */
                }
                log_info("dead ops: pause button (frame saved to build/doa-pause.ppm)");
            } else {
                key_set(XPERIA_KEY_PAUSE, 1);
                g_pause_release_at = monotonic_ms() + MENU_TOUCH_HOLD_MS;
                if (g_native_pause_mode) {
                    g_native_pause_open = true;
                    g_native_pause_close_at = 0;
                }
            }
            break;
        }
        if (sc == SDL_SCANCODE_ESCAPE && down && native_pause_active()) {
            input_tap(221, 640); /* RETURN */
            native_pause_close();
            break;
        }
        if (!g_captured && down) {
            int dir = sc == SDL_SCANCODE_LEFT ? 0 : sc == SDL_SCANCODE_RIGHT ? 1
                      : sc == SDL_SCANCODE_UP ? 2 : sc == SDL_SCANCODE_DOWN ? 3 : -1;
            if (dir >= 0 && menus_nav(dir)) {
                menus_set_pad_mode(true);
                break;
            }
            if ((sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) && menus_activate()) {
                break;
            }
        }
        /* Escape: the pause binding handles gameplay; in menus it is "back" */
        if (sc == SDL_SCANCODE_ESCAPE && !g_captured) {
            if (down && menus_back()) {
                break;
            }
            key_set(S3E_KEY_BACK, down);
        }
        break;
    }
    case SDL_MOUSEMOTION:
        if (g_captured) {
            g_mouse_dx += ev->motion.xrel;
            g_mouse_dy += ev->motion.yrel;
        } else {
            menus_set_pad_mode(false);
            float rx, ry;
            ui_window_to_ref(ev->motion.x, ev->motion.y, &rx, &ry);
            menus_hover(rx, ry);
            if (!g_tap_release_at && !g_menu_mouse_owned && !g_mouse_release_pending &&
                menus_pointer_passthrough(rx, ry)) {
                int32_t x, y;
                window_to_surface(ev->motion.x, ev->motion.y, &x, &y);
                pointer_motion(x, y);
            }
        }
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        int down = ev->type == SDL_MOUSEBUTTONDOWN;
        if (ev->button.button == SDL_BUTTON_LEFT && down && native_pause_active()) {
            int32_t px, py;
            window_to_surface(ev->button.x, ev->button.y, &px, &py);
            if (px < 420 && py > 570) { /* RETURN, bottom left */
                native_pause_close();
            }
        }
        if (ev->button.button == SDL_BUTTON_LEFT && !down) {
            g_menu_mouse_down = false;
            if (menu_mouse_owns_release()) break;
            if (g_mouse_touch_active) {
                g_mouse_release_pending = true;
                mouse_touch_update();
                break;
            }
        }
        /* in gameplay the screen has no touch controls we want: a click grabs the mouse */
        /* also after Esc/Tab/Alt-Tab: in co-op the pause key opens no menu, so g_manual would
         * otherwise stay set and the click would never give the camera back */
        int want_click = wants_capture();
        if (want_click < 0 && reimagined_deadops()) {
            want_click = 1; /* original build (no match signal): Dead Ops gameplay is known */
        }
        if (!g_captured && down && want_click == 1) {
            g_manual = false;
            set_captured(true);
            break;
        }
        if (!g_captured) {
            if (ev->button.button == SDL_BUTTON_LEFT && down) {
                if (!menu_mouse_begin()) break;
                if (menu_mouse_touch_busy()) {
                    g_menu_mouse_owned = true;
                    break;
                }
                float rx, ry;
                ui_window_to_ref(ev->button.x, ev->button.y, &rx, &ry);
                menus_set_pad_mode(false);
                if (menus_click(rx, ry)) {
                    g_menu_mouse_owned = true;
                    break;
                }
                /* title: the main menu would get this touch late at the same spot */
                if (screens_is("title") || screens_is("title_prompt")) {
                    if (!g_tap_release_at) {
                        input_tap(640, 650);
                    }
                    g_menu_mouse_owned = true;
                    break;
                }
            }
            if (ev->button.button == SDL_BUTTON_LEFT && down && !g_tap_release_at &&
                !(screens_is("title") || screens_is("title_prompt"))) {
                int32_t x, y;
                window_to_surface(ev->button.x, ev->button.y, &x, &y);
                pointer_motion(x, y);
                g_mouse_touch_active = true;
                pointer_button(1);
            }
            break;
        }
        break;
    }
    case SDL_TEXTINPUT:
        if (menus_editing_name()) {
            menus_name_text(ev->text.text);
        }
        break;
    case SDL_CONTROLLERBUTTONDOWN:
        if (!g_captured) {
            SDL_GameControllerButton b = (SDL_GameControllerButton)ev->cbutton.button;
            int dir = b == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? 0 : b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ? 1
                      : b == SDL_CONTROLLER_BUTTON_DPAD_UP ? 2 : b == SDL_CONTROLLER_BUTTON_DPAD_DOWN ? 3 : -1;
            menus_set_pad_mode(true);
            if (dir >= 0) {
                menus_nav(dir);
            } else if (b == SDL_CONTROLLER_BUTTON_A) {
                if (!menus_activate()) {
                    /* the title and unmapped screens: A still starts / confirms */
                    key_set(XPERIA_KEY_ACTION_SPRINT, 1);
                    key_set(XPERIA_KEY_ACTION_SPRINT, 0);
                }
            } else if (b == SDL_CONTROLLER_BUTTON_B) {
                if (!menus_back()) {
                    key_set(S3E_KEY_BACK, 1);
                    key_set(S3E_KEY_BACK, 0);
                }
            }
        }
        break;
    case SDL_MOUSEWHEEL:
        if (g_captured) {
            bindings_wheel(ev->wheel.y);
        }
        break;
    case SDL_CONTROLLERDEVICEADDED:
        if (!g_controller) {
            g_controller = SDL_GameControllerOpen(ev->cdevice.which);
            if (g_controller) {
                log_info("controller: %s", SDL_GameControllerName(g_controller));
            }
        }
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (g_controller &&
            ev->cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_controller))) {
            SDL_GameControllerClose(g_controller);
            g_controller = NULL;
        }
        break;
    default:
        break;
    }
}

static float axis(SDL_GameControllerAxis a) {
    int v = g_controller ? SDL_GameControllerGetAxis(g_controller, a) : 0;
    if (v > -AXIS_DEADZONE && v < AXIS_DEADZONE) {
        return 0.0f;
    }
    float f = (float)(v > 0 ? v - AXIS_DEADZONE : v + AXIS_DEADZONE) / (32767.0f - AXIS_DEADZONE);
    return f < -1.0f ? -1.0f : f > 1.0f ? 1.0f : f;
}

static void set_captured(bool captured);
static void handle_event(const SDL_Event *ev);

static void script_load(void) {
    const char *text = getenv("CODBOZ_SCRIPT");
    while (text && *text && g_script_count < 256) {
        struct script_step *step = &g_script[g_script_count];
        int used = 0;
        step->a = step->b = 0;
        if (sscanf(text, " %f %15s %f %f%n", &step->time, step->cmd, &step->a, &step->b, &used) >= 2 ||
            sscanf(text, " %f %15s %f%n", &step->time, step->cmd, &step->a, &used) >= 2) {
            char *semi_in_cmd = strchr(step->cmd, ';');
            if (semi_in_cmd) {
                *semi_in_cmd = 0; /* "0 @title;" */
            }
            g_script_count++;
        }
        const char *semi = strchr(text, ';');
        text = semi ? semi + 1 : NULL;
    }
    g_script_start = monotonic_ms();
}

static void script_run(void) {
    uint64_t now = monotonic_ms();
    if (g_script_release_at && now >= g_script_release_at) {
        g_script_release_at = 0;
        pointer_button(0);
    }
    while (g_script_next < g_script_count &&
           (float)(now - g_script_start) / 1000.0f >= g_script[g_script_next].time) {
        struct script_step *step = &g_script[g_script_next];
        if (step->cmd[0] == '@') {
            /* "@screen" (or "@match") waits for it; later times count from that moment */
            bool ok = strcmp(step->cmd + 1, "match") == 0 ? reimagined_match_active()
                                                          : screens_is(step->cmd + 1);
            if (!ok) {
                break;
            }
            g_script_next++;
            g_script_start = now;
            log_info("script: reached %s", step->cmd + 1);
            continue;
        }
        g_script_next++;
        log_info("script: %s %g %g", step->cmd, step->a, step->b);
        if (strcmp(step->cmd, "click") == 0 && menus_click(step->a, step->b)) {
            /* taken by a redrawn menu */
        } else if (strcmp(step->cmd, "click") == 0) {
            pointer_motion((int32_t)step->a, (int32_t)step->b);
            pointer_button(1);
            g_script_release_at = now + 120;
        } else if (strcmp(step->cmd, "shot") == 0) {
            /* "T shot": save this frame as $CODBOZ_SHOT_DIR/shot-T.ppm */
            const char *dir = getenv("CODBOZ_SHOT_DIR");
            char path[1100];
            snprintf(path, sizeof(path), "%s/shot-%06.1f.ppm", dir ? dir : ".",
                     (double)(now - g_script_start) / 1000.0);
            extern void video_request_shot(const char *path);
            video_request_shot(path);
        } else if (strcmp(step->cmd, "tap") == 0) {
            input_tap(step->a, step->b);
        } else if (strcmp(step->cmd, "key") == 0) {
            key_set((uint32_t)step->a, step->b != 0);
        } else if (strcmp(step->cmd, "move") == 0) {
            g_script_move_x = step->a;
            g_script_move_y = step->b;
        } else if (strcmp(step->cmd, "look") == 0) {
            g_mouse_dx += (int)step->a;
            g_mouse_dy += (int)step->b;
        } else if (strcmp(step->cmd, "capture") == 0) {
            set_captured(step->a != 0);
        } else if (strcmp(step->cmd, "mmove") == 0) {
            SDL_Event e;
            memset(&e, 0, sizeof e);
            e.type = SDL_MOUSEMOTION; e.motion.x = (int)step->a; e.motion.y = (int)step->b;
            handle_event(&e);
        } else if (strcmp(step->cmd, "esc") == 0 || strcmp(step->cmd, "blur") == 0 ||
                   strcmp(step->cmd, "mclick") == 0) {
            /* real event paths (Esc, losing focus, a mouse click) for regression scripts */
            SDL_Event e;
            memset(&e, 0, sizeof e);
            if (step->cmd[0] == 'e') {
                e.type = SDL_KEYDOWN; e.key.keysym.scancode = SDL_SCANCODE_ESCAPE; handle_event(&e);
                e.type = SDL_KEYUP; handle_event(&e);
            } else if (step->cmd[0] == 'b') {
                e.type = SDL_WINDOWEVENT; e.window.event = SDL_WINDOWEVENT_FOCUS_LOST; handle_event(&e);
            } else {
                e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT;
                e.button.x = (int)step->a; e.button.y = (int)step->b; handle_event(&e);
                e.type = SDL_MOUSEBUTTONUP; handle_event(&e);
            }
        }
    }
}

static void update_gameplay(float dt) {
    /* movement: WASD or left stick */
    const uint8_t *keys = SDL_GetKeyboardState(NULL);
    float mx = 0, my = 0;
    (void)keys;
    if (g_captured) {
        mx = (float)(bindings_down(BIND_RIGHT, true, NULL) - bindings_down(BIND_LEFT, true, NULL));
        my = (float)(bindings_down(BIND_BACK, true, NULL) - bindings_down(BIND_FORWARD, true, NULL));
        if (mx && my) {
            mx *= 0.7071f;
            my *= 0.7071f;
        }
    }
    if (g_script_move_x || g_script_move_y) {
        mx = g_script_move_x;
        my = g_script_move_y;
    }
    float sx = axis(SDL_CONTROLLER_AXIS_LEFTX), sy = axis(SDL_CONTROLLER_AXIS_LEFTY);
    if (sx || sy) {
        mx = sx;
        my = sy;
    }

    /* look: mouse velocity becomes stick deflection, or the right stick */
    float lx = axis(SDL_CONTROLLER_AXIS_RIGHTX), ly = axis(SDL_CONTROLLER_AXIS_RIGHTY);
    bool deadops = g_captured && reimagined_deadops();
    /*
     * Several pumps run per frame. Stick look samples once per frame, with the frame's dt, and
     * holds until the next swap: the game must not see the deflection decay or the pad lift
     * between its own reads.
     */
    bool look_hold = g_captured && !deadops && !g_opt.mouse_drag && !g_look_frame_start;
    static int logged_deadops = -1;
    if ((int)reimagined_deadops() != logged_deadops) {
        logged_deadops = reimagined_deadops();
        log_info("dead ops arcade: %s", logged_deadops ? "detected" : "no");
    }
    if (deadops) {
        /*
         * Dead Ops Arcade is twin-stick: the right pad aims AND fires. The mouse steers a
         * persistent aim direction; it only fires while "shoot" is held.
         */
        static float aim_x = 1.0f, aim_y = 0.0f;
        float k = 0.01f * g_opt.mouse_sensitivity;
        aim_x += (float)g_mouse_dx * k;
        aim_y += (float)g_mouse_dy * k * (g_opt.invert_y ? -1.0f : 1.0f);
        float len = sqrtf(aim_x * aim_x + aim_y * aim_y);
        if (len > 1.0f) {
            aim_x /= len;
            aim_y /= len;
        } else if (len < 0.35f && len > 0.0001f) {
            aim_x *= 0.35f / len; /* keep a direction: never collapse to the centre */
            aim_y *= 0.35f / len;
        }
        g_mouse_dx = g_mouse_dy = 0;
        g_look_x = g_look_y = 0;
        if (g_drag_down) {
            drag_release();
        }
        if (!lx && !ly && bindings_down(BIND_SHOOT, true, NULL)) {
            float n = sqrtf(aim_x * aim_x + aim_y * aim_y);
            lx = aim_x / n;
            ly = aim_y / n;
        }
    } else if (g_captured && g_opt.mouse_drag) {
        drag_update(g_mouse_dx, g_mouse_dy);
        g_mouse_dx = g_mouse_dy = 0;
    } else if (look_hold) {
        /* keep the deflection; motion accumulates for the next frame */
    } else if (g_captured && (g_mouse_dx || g_mouse_dy)) {
        float scale = 0.045f * g_opt.mouse_sensitivity / (dt > 0.001f ? dt * 60.0f : 1.0f);
        g_look_x = (float)g_mouse_dx * scale;
        g_look_y = (float)g_mouse_dy * scale * g_opt.mouse_y_scale * (g_opt.invert_y ? -1.0f : 1.0f);
    } else {
        g_look_x *= 0.35f;
        g_look_y *= 0.35f;
        if (fabsf(g_look_x) < 0.02f) {
            g_look_x = 0;
        }
        if (fabsf(g_look_y) < 0.02f) {
            g_look_y = 0;
        }
    }
    if (!look_hold) {
        g_mouse_dx = g_mouse_dy = 0;
    }
    if (!lx && !ly) {
        lx = g_look_x < -1 ? -1 : g_look_x > 1 ? 1 : g_look_x;
        ly = g_look_y < -1 ? -1 : g_look_y > 1 ? 1 : g_look_y;
    }

    if (g_captured || g_controller || g_script_count) {
        pad_stick(0, mx, my, TOUCHPAD_WIDTH / 5, TOUCHPAD_HEIGHT / 2, TOUCHPAD_WIDTH / 5,
                  TOUCHPAD_HEIGHT / 2);
        if (g_drag_down && !lx && !ly) {
            /* the drag owns the look pad */
        } else {
            if (g_drag_down) {
                drag_release();
            }
            pad_stick(1, lx, ly, TOUCHPAD_WIDTH * 4 / 5, TOUCHPAD_HEIGHT / 2, TOUCHPAD_WIDTH / 8,
                      TOUCHPAD_WIDTH / 8);
        }
    }

    static uint8_t last[BIND_COUNT];
    static int leave_aim_frames;
    if (g_pause_release_at && monotonic_ms() >= g_pause_release_at) {
        g_pause_release_at = 0;
        key_set(XPERIA_KEY_PAUSE, 0);
    }
    if (leave_aim_frames > 0 && --leave_aim_frames == 0) {
        key_set(XPERIA_KEY_LEAVE_AIM, 0);
    }
    for (int a = BIND_SHOOT; a < BIND_COUNT; ++a) {
        /* in menus the controller drives the focus (handle_event), not the game keys */
        uint8_t down = (uint8_t)bindings_down((enum bind_action)a, g_captured,
                                               g_captured || a == BIND_PAUSE ? g_controller : NULL);
        if (a == BIND_SCOREBOARD) {
            reimagined_set_scoreboard(down != 0); /* drawn by the host: nothing reaches the game */
            continue;
        }
        if (a == BIND_PAUSE && !g_captured && !g_controller) {
            down = 0; /* Escape in menus is "back", handled as an event */
        }
        if (a == BIND_PAUSE && g_captured && reimagined_deadops()) {
            if (down && !last[a]) {
                input_tap(1232, 44); /* controller Start in Dead Ops: its on-screen pause button */
            }
            last[a] = down;
            continue;
        }
        bool deadops = g_captured && reimagined_deadops();
        if (a == BIND_SHOOT && deadops) {
            down = 0; /* Dead Ops: the shoot key is the special weapon (nuke); the aim pad fires */
        }
        if ((a == BIND_NUKE || a == BIND_BOOST) && !deadops) {
            down = 0; /* outside Dead Ops these would fire / aim the gun */
        }
        if (a == BIND_AIM && deadops) {
            down = 0; /* Dead Ops: the aim key is the boost, which has its own action */
        }
        if (deadops && (a == BIND_MELEE || a == BIND_GRENADE) &&
            (bindings_down(BIND_NUKE, true, g_controller) || bindings_down(BIND_BOOST, true, g_controller))) {
            down = 0; /* buttons the nuke/boost share with the FPS maps' melee/grenade */
        }
        if (down != last[a]) {
            key_set(k_action_key[a], down);
            last[a] = down;
            if (a == BIND_RELOAD) {
                log_debug("reload key %u %s", k_action_key[a], down ? "down" : "up");
            }
            /* hold to aim: the reimagined binary toggles on press and leaves aim on key 15 */
            if ((a == BIND_AIM || a == BIND_BOOST) && !down && reimagined_patched()) {
                key_set(XPERIA_KEY_LEAVE_AIM, 1);
                leave_aim_frames = 2;
            }
        }
    }
    bindings_end_frame();
}

void input_pump(void) {
    static uint64_t last_us;
    if (g_pumping) {
        return;
    }
    g_pumping = 1;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        handle_event(&ev);
    }
    uint64_t now = monotonic_us();
    float dt = last_us ? (float)(now - last_us) / 1e6f : 1.0f / 60.0f;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    last_us = now;
    int want = wants_capture();
    if (want >= 0 && want != g_auto_capture) {
        g_auto_capture = want;
        g_manual = false;
        set_captured(want != 0);
    }
    mouse_touch_update();
    tap_update();
    if (g_script_count) {
        script_run();
    }
    g_look_dt = g_look_dt + dt > 0.1f ? 0.1f : g_look_dt + dt;
    update_gameplay(g_look_dt);
    if (g_look_frame_start) {
        g_look_frame_start = false;
        g_look_dt = 0;
    }
    g_pumping = 0;
}

void input_after_swap(void) {
    /* Touch timing uses monotonic milliseconds and guest input acknowledgement. */
    g_look_frame_start = true;
}

bool input_init(void) {
    g_input_debug = getenv("CODBOZ_INPUT_DEBUG") != NULL;
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i) && !g_controller) {
            g_controller = SDL_GameControllerOpen(i);
            if (g_controller) {
                log_info("controller: %s", SDL_GameControllerName(g_controller));
            }
        }
    }
    g_pointer_x = video_width() / 2;
    g_pointer_y = video_height() / 2;
    script_load();
    return true;
}

/* ---- imports ---- */

IMPORT(s3eKeyboardRegister) {
    slot_register(g_keyboard_cb, COUNT(g_keyboard_cb));
}

IMPORT(s3eKeyboardUnRegister) {
    slot_unregister(g_keyboard_cb, COUNT(g_keyboard_cb));
}

IMPORT(s3eKeyboardUpdate) {
    input_pump();
    memcpy(g_key_published, g_key_live, sizeof(g_key_published));
    for (int i = 0; i < KEYBOARD_KEY_COUNT; ++i) {
        g_key_live[i] &= (uint8_t)~(KEY_STATE_PRESSED | KEY_STATE_RELEASED);
    }
    dispatch_due_timers();
    ret(0);
}

static uint32_t abs_key(uint32_t key) {
    static const uint32_t map[] = {
        XPERIA_KEY_ACTION_SPRINT, XPERIA_KEY_RELOAD_PATCHED, XPERIA_KEY_THROW_GRENADE,
        XPERIA_KEY_MELEE, XPERIA_KEY_TACTICAL_GRENADE, XPERIA_KEY_CROUCH_PRONE, XPERIA_KEY_LEAVE_AIM,
        XPERIA_KEY_CHANGE_WEAPON, XPERIA_KEY_ACTION_SPRINT, XPERIA_KEY_PAUSE, S3E_KEY_BACK,
    };
    return key >= S3E_KEY_ABS_GAME_A && key <= S3E_KEY_ABS_BSK ? map[key - S3E_KEY_ABS_GAME_A]
                                                                : key;
}

IMPORT(s3eKeyboardGetState) {
    uint32_t key = abs_key(arg(0));
    ret(key < KEYBOARD_KEY_COUNT ? g_key_published[key] : 0);
}

IMPORT(s3eKeyboardAnyKey) {
    for (uint32_t i = 0; i < KEYBOARD_KEY_COUNT; ++i) {
        if (g_key_published[i] & KEY_STATE_PRESSED) {
            ret(i);
            return;
        }
    }
    ret(0);
}

IMPORT(s3eKeyboardGetInt) {
    switch (arg(0)) {
    case 0: /* HAS_NUMPAD */
    case 2: /* HAS_DIRECTION */
        ret(1);
        break;
    case 1:
    case 4:
    case 5:
    case 6:
        ret(0);
        break;
    default:
        ret((uint32_t)-1);
        break;
    }
}

IMPORT(s3eKeyboardSetInt) {
    ret(0);
}

IMPORT(s3eKeyboardClearState) {
    memset(g_key_live, 0, sizeof(g_key_live));
    memset(g_key_published, 0, sizeof(g_key_published));
}

IMPORT(s3eKeyboardGetDisplayName) {
    static uint32_t empty;
    if (!empty) {
        empty = arena_strdup("");
    }
    ret(empty);
}

IMPORT(s3ePointerRegister) {
    slot_register(g_pointer_cb, COUNT(g_pointer_cb));
}

IMPORT(s3ePointerUnRegister) {
    slot_unregister(g_pointer_cb, COUNT(g_pointer_cb));
}

IMPORT(s3ePointerUpdate) {
    input_pump();
    pointer_publish();
    dispatch_due_timers();
    ret(0);
}

IMPORT(s3ePointerGetInt) {
    switch (arg(0)) {
    case 0: /* AVAILABLE */
        ret(1);
        break;
    case 1: /* BUTTON_COUNT */
        ret(1);
        break;
    case 2: /* MULTI_TOUCH_AVAILABLE */
        ret(1);
        break;
    case 4: /* TYPE: stylus/touch */
        ret(1);
        break;
    default:
        ret(0);
        break;
    }
}

IMPORT(s3ePointerSetInt) {
    ret(0);
}

IMPORT(s3ePointerGetState) {
    ret(arg(0) == 0 ? g_pointer_published : 0);
}

IMPORT(s3ePointerGetX) {
    ret((uint32_t)g_pointer_x);
}

IMPORT(s3ePointerGetY) {
    ret((uint32_t)g_pointer_y);
}

IMPORT(s3eTouchpadRegister) {
    slot_register(g_touchpad_cb, COUNT(g_touchpad_cb));
}

IMPORT(s3eTouchpadUnRegister) {
    slot_unregister(g_touchpad_cb, COUNT(g_touchpad_cb));
}

IMPORT(s3eTouchpadZero) {
    ret(0);
}

IMPORT(s3eTouchpadGetInt) {
    switch (arg(0)) {
    case 0: /* AVAILABLE */
        ret(1);
        break;
    case 1:
        ret(TOUCHPAD_WIDTH);
        break;
    case 2:
        ret(TOUCHPAD_HEIGHT);
        break;
    default:
        ret((uint32_t)-1);
        break;
    }
}

void touchpad_fill_interface(uint32_t *table) {
    table[0] = import_thunk("s3eTouchpadRegister", imp_s3eTouchpadRegister);
    table[1] = import_thunk("s3eTouchpadUnRegister", imp_s3eTouchpadUnRegister);
    table[2] = import_thunk("s3eTouchpadZero1", imp_s3eTouchpadZero);
    table[3] = import_thunk("s3eTouchpadZero2", imp_s3eTouchpadZero);
    table[4] = import_thunk("s3eTouchpadGetInt", imp_s3eTouchpadGetInt);
}

void register_input_imports(void) {
    REG(s3eKeyboardRegister);
    REG(s3eKeyboardUnRegister);
    REG(s3eKeyboardUpdate);
    REG(s3eKeyboardGetState);
    REG(s3eKeyboardAnyKey);
    REG(s3eKeyboardGetInt);
    REG(s3eKeyboardSetInt);
    REG(s3eKeyboardClearState);
    REG(s3eKeyboardGetDisplayName);
    REG(s3ePointerRegister);
    REG(s3ePointerUnRegister);
    REG(s3ePointerUpdate);
    REG(s3ePointerGetInt);
    REG(s3ePointerSetInt);
    REG(s3ePointerGetState);
    REG(s3ePointerGetX);
    REG(s3ePointerGetY);
}
