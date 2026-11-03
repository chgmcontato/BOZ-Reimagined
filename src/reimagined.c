/*
 * The boz-reimagined TV layer (Hud.java and parts of Cursor/StyledScreen), redrawn
 * by the host. It reads the state the patched binary publishes:
 *   0x410700 match timer ("1MIT", +8 active, +12/16 elapsed ms, +20/24 total s/ms,
 *            +32 profile flag, +48 pause flag written by this layer)
 *   0x4108f0 tactical bank (+8 = player), 0x4958c4 wave manager, 0x4958d0 level manager
 * Offsets are image offsets; runtime address = 0x4a000000 + offset - 0x35090.
 */
#include "reimagined.h"
#include "host.h"
#include "screens.h"
#include "ui.h"
#include "coop_pause_state.h"
#include "coop_stats.h"

#include <ctype.h>
#include <stdlib.h>

#define IMG(off) (0x4a000000u + (uint32_t)(off) - 0x35090u)

enum {
    TIMER = 0x410700,
    BANK = 0x4108f0,
    WAVES = 0x4958c4,
    LEVEL = 0x4958d0,
    FRAG = 0x595f545a,
    MONKEY = 0x1561c758,
    CLAYMORE = 0x3dc9e501,
    GERSH = 0x75121c4e,
    MATRYOSHKA = 0x832c1d12,
    ITEMS = 5,
};

static bool g_enabled;
static struct ui_image g_icons[ITEMS], g_digits[10];
static bool g_timer_shown = true;
static int g_round = -1;
static char g_map[32];

static uint32_t rd(uint32_t addr) {
    /* only read inside the image or the guest heap */
    if (!((addr >= 0x4a000000u && addr < 0x4a500000u) ||
          (addr >= GUEST_HEAP_BASE && addr < (uint32_t)(uint32_t)GUEST_HEAP_BASE + (uint32_t)GUEST_HEAP_SIZE - 4))) {
        return 0;
    }
    return *(volatile uint32_t *)G2H(addr);
}

static int sb_remote_count(void);
static struct coop_pause_state g_coop_pause;
static int g_party; /* most players seen in this match (connections can include stale entries) */
static uint32_t g_owned_pause;
static bool g_test_hold; /* CODBOZ_TEST_FREEZE holding right now */
static uint32_t ingame_state(void) {
    uint32_t app=rd(IMG(0x491784));
    if(!app || rd(app)!=IMG(0x42f250))return 0;
    uint32_t entries=rd(app+0x44), count=rd(app+0x48);
    if(!entries || count>128)return 0;
    for(uint32_t i=0;i<count;i++) if(rd(entries+i*8)==0) {
        uint32_t wrapper=rd(entries+i*8+4), state=wrapper?rd(wrapper+4):0;
        return state && rd(state)==IMG(0x42f708)?state:0;
    }
    return 0;
}
/*
 * The round's counters, from WaveManagerStateInfo's reflection table (the state starts at +0x6c).
 * Logged around a co-op pause: the round must come back as it went in.
 */
struct round_snapshot {
    int zombies, powered, wave, killed, countdown;
};

static void round_snapshot(struct round_snapshot *s) {
    uint32_t waves = rd(IMG(0x4958c4));
    memset(s, 0, sizeof(*s));
    if (waves) {
        s->zombies = (int)rd(waves + 0x70);
        s->powered = (int)rd(waves + 0x74);
        s->wave = (int)rd(waves + 0x78);
        s->killed = (int)rd(waves + 0xa0);
        s->countdown = (int)rd(waves + 0x98);
    }
}

static void round_log(const char *tag, const struct round_snapshot *s) {
    log_info("round state %s: wave %d, zombieCounter %d, poweredZombieCounter %d, zombiesKilled %d, countdown %d",
             tag, s->wave, s->zombies, s->powered, s->killed, s->countdown);
}

static void sync_coop_pause(bool active) {
    uint32_t state=ingame_state(), network=rd(IMG(0x4958d4));
    if(active && !state)return;
    bool before=g_coop_pause.waiting && !g_test_hold;
    /* no network object in a live co-op match means nobody is connected */
    bool waiting=coop_pause_update(&g_coop_pause,active,state,active&&network?(int)rd(network+0x64):0,monotonic_ms());
    /* re-assert after a native pause/unpause so the freeze cannot be bypassed */
    if(waiting && state && (rd(state+0x50)&255u)==0) {
        *(volatile uint8_t *)G2H(state+0x50)=1;g_owned_pause=state;
    } else if(!waiting && g_owned_pause) {
        if(state==g_owned_pause && (rd(state+0x50)&255u)==1)
            *(volatile uint8_t *)G2H(state+0x50)=0;
        g_owned_pause=0;
    }
    /* every change a reconnection test needs to compare: round, party and this player's game state */
    if(active && state) {
        static int last_wave=-1,last_peers=-1,last_sub=-1,last_rejoin=-1;
        struct round_snapshot snap;
        round_snapshot(&snap);
        uint32_t level=rd(IMG(0x4958d0));
        int sub=(int)rd(state+0x10),rejoin=level?(int)(rd(level+0x208)>>8&0xffu):-1,peers=g_coop_pause.peers;
        if(snap.wave!=last_wave||peers!=last_peers||sub!=last_sub||rejoin!=last_rejoin) {
            log_info("coop state: peers %d of %d, remotes %d, ingame state %d, rejoined level %d, waiting %d",
                     peers,g_coop_pause.baseline,sb_remote_count(),sub,rejoin,g_coop_pause.waiting);
            round_log(snap.wave!=last_wave?"wave change":"event",&snap);
            last_wave=snap.wave;last_peers=peers;last_sub=sub;last_rejoin=rejoin;
        }
    }
    if(before!=waiting) {
        static struct round_snapshot at_pause;
        struct round_snapshot now;
        round_snapshot(&now);
        round_log(waiting?"at pause":"at resume",&now);
        if(waiting) at_pause=now;
        else if(memcmp(&at_pause,&now,sizeof(now))!=0) log_info("round state: CHANGED during the pause");
    }
    if(before!=waiting)log_info("co-op: %s (%d of %d players)",waiting?"match paused, waiting for missing player":"party restored, match resumed",g_coop_pause.peers,g_coop_pause.baseline);
}

bool reimagined_match_waiting(void) {
    return g_coop_pause.waiting;
}

static bool timer_valid(void) {
    return memcmp(G2H(IMG(TIMER)), "1MIT", 4) == 0;
}

bool reimagined_match_active(void) {
    return timer_valid() && rd(IMG(TIMER) + 8) == 1;
}

/* 1 co-op, 0 solo, -1 unknown (no match running or no level yet). */
int reimagined_match_coop(void) {
    uint32_t level = reimagined_match_active() ? rd(IMG(LEVEL)) : 0;
    return level ? (rd(level + 0x208) & 0xffu) != 0 : -1;
}

/* The game's own "a session can be re-joined" flag (network+0x88, what setEnableReconnect gets). */
bool reimagined_rejoin_available(void) {
    uint32_t net = rd(IMG(0x4958d4));
    return net && (rd(net + 0x88) & 0xffu) != 0;
}

bool reimagined_patched(void) {
    return timer_valid();
}

void reimagined_init(const char *assets_dir) {
    char path[1200];
    static const char *names[ITEMS] = {"frag", "monkey", "claymore", "gersch", "matryoshka"};
    snprintf(path, sizeof(path), "%s/boz_menus.json", assets_dir);
    screens_load(path);
    snprintf(path, sizeof(path), "%s/character-menu/comic.ttf", assets_dir);
    ui_load_font(path);
    snprintf(path, sizeof(path), "%s/hud/d0.png", assets_dir);
    g_enabled = ui_image_load(&g_digits[0], path);
    for (int i = 1; g_enabled && i < 10; ++i) {
        snprintf(path, sizeof(path), "%s/hud/d%d.png", assets_dir, i);
        ui_image_load(&g_digits[i], path);
    }
    for (int i = 0; g_enabled && i < ITEMS; ++i) {
        snprintf(path, sizeof(path), "%s/hud/%s.png", assets_dir, names[i]);
        ui_image_load(&g_icons[i], path);
    }
    log_info("reimagined layer: %s", g_enabled ? "on" : "off (no TV assets)");
}

bool reimagined_enabled(void) {
    return g_enabled;
}

void reimagined_toggle_timer(void) {
    g_timer_shown = !g_timer_shown;
}

bool reimagined_timer_shown(void) {
    return g_timer_shown;
}

/*
 * Dead Ops Arcade, in both the original and the reimagined build (it does not need the timer
 * patch): a level is loaded but there is no zombie-wave manager. Both are static slots in the
 * game image; the level pointer must point into the guest heap to count.
 */
/*
 * Co-op match from the game's own network state, independent of which menus were recognised on
 * the way in (the co-op character pick can look like the solo one and clear that flag): two or
 * more players connected during this match. The peak is kept so a dropped partner still counts.
 */
bool reimagined_coop_match(void) {
    static int peak;
    if (!reimagined_match_active()) {
        peak = 0;
        return false;
    }
    uint32_t net = rd(IMG(0x4958d4));
    if (net >= GUEST_HEAP_BASE && net < (uint32_t)GUEST_HEAP_BASE + (uint32_t)GUEST_HEAP_SIZE) {
        int peers = (int)rd(net + 0x64);
        if (peers > peak && peers <= 16) {
            peak = peers;
        }
    }
    return peak >= 2;
}

/*
 * Zombies per round. The reimagined patch (patch_s3e.py, CAVE2 "zcount" at file 0x410900) uses
 * BO1 *solo* x 15/24 for everyone, so co-op had solo counts. Same formula with the player count:
 *   mult = R/5 (at least 1); from R10 mult *= 0.15 R      -> 0.03 R^2
 *   max  = 24 + int(k * 6 * mult), k = 0.5 solo, players-1 in co-op   (P = 0.09/0.18/0.36/0.54)
 *   R1-5 x 0.25/0.3/0.5/0.7/0.9; reimagined = round(max * 15/24) (same hordes, horde of 15)
 */
int reimagined_zombie_count(int round, int players) {
    if (round < 1) {
        round = 1;
    }
    if (round > 20000) {
        round = 20000;
    }
    if (players < 1) {
        players = 1;
    }
    if (players > 4) {
        players = 4;
    }
    if (round >= 10) {
        /* integer form of 24 + P R^2 scaled by 15/24, as the reimagined patch computes solo:
         * (2400 + 9 m R^2 + 80) / 160, m = 1 solo, 2/4/6 for 2/3/4 players (same as the TV patch) */
        uint64_t m = players == 1 ? 1u : 2u * (uint64_t)(players - 1);
        return (int)((2400u + 9u * m * (uint64_t)round * (uint64_t)round + 80u) / 160u);
    }
    double mult = round / 5.0;
    if (mult < 1.0) {
        mult = 1.0;
    }
    if (round >= 10) {
        mult *= round * 0.15;
    }
    double k = players == 1 ? 0.5 : (double)(players - 1);
    double max = 24.0 + (double)(long long)(k * 6.0 * mult);
    static const double early[] = {0.25, 0.3, 0.5, 0.7, 0.9};
    if (round <= 5) {
        max = (double)(long long)(max * early[round - 1]);
    }
    return (int)(max * 15.0 / 24.0 + 0.5);
}

/*
 * Real players: the remote-player map + this device. network+0x64 counts connections and may
 * include a stale one, so it only decides "at least two" before the remote players exist.
 * g_party keeps a dropped partner counted, as the match waits for them.
 */
static int sb_remote_count(void);
static int party_size(void) {
    int n = sb_remote_count() + 1;
    if (n < 2) {
        uint32_t net = rd(IMG(0x4958d4));
        if (net >= GUEST_HEAP_BASE && net < (uint32_t)GUEST_HEAP_BASE + (uint32_t)GUEST_HEAP_SIZE &&
            (int)rd(net + 0x64) >= 2) {
            n = 2;
        }
    }
    if (n < g_party) {
        n = g_party;
    }
    return n > 4 ? 4 : n;
}

static void zcount_thunk(void) {
    /* solo is always 1: the network object keeps the previous co-op party size */
    int round = (int)arg(1), players = screens_coop_session() ? party_size() : 1;
    int count = reimagined_zombie_count(round, players);
    static int last_round = -1, last_players = -1;
    if (round != last_round || players != last_players) {
        last_round = round;
        last_players = players;
        log_info("zombies: round %d, %d player(s) -> %d", round, players, count);
    }
    ret((uint32_t)count);
}

/* The reimagined build keeps the engine's debug frame counter (IwGxPrintFrameRate) always on; the
 * port draws its own (F3 / launcher), so the native one is turned into "bx lr". */
static void hide_native_fps(void) {
    uint32_t *fn = G2H(IMG(0x2ad888));
    if (*fn != 0xe59f307cu) { /* ldr r3, [pc, #0x7c] */
        log_info("fps: native counter not found");
        return;
    }
    *fn = 0xe12fff1eu; /* bx lr (ARM) */
}

/* Reimagined build only: route its zcount to zcount_thunk (keeps the solo numbers, adds co-op). */
void reimagined_patch_waves(void) {
    hide_native_fps();
    uint8_t *fn = G2H(IMG(0x410900));
    static const uint8_t expect[] = {0x09, 0x29, 0x02, 0xd8, 0x0b, 0xa2, 0x50, 0x5c};
    if (memcmp(fn, expect, sizeof(expect)) != 0) {
        log_info("zombies: reimagined zcount not found, wave sizes unchanged");
        return;
    }
    uint32_t thunk = emu_add_thunk("reimagined_zcount", zcount_thunk);
    static const uint8_t ldr_pc[] = {0xdf, 0xf8, 0x00, 0xf0}; /* ldr.w pc, [pc, #0] (Thumb) */
    memcpy(fn, ldr_pc, 4);
    memcpy(fn + 4, &thunk, 4); /* ARM thunk (even address): ldr pc interworks to ARM */
    log_info("zombies: wave size now follows the player count (solo P 0.09, 2p 0.18, 3p 0.36, 4p 0.54)");
    if (getenv("CODBOZ_DUMP")) { /* "hexoff,len": raw guest bytes of the game image to dump.bin (diagnostics) */
        unsigned off = 0, len = 0;
        if (sscanf(getenv("CODBOZ_DUMP"), "%x,%u", &off, &len) == 2 && len < 0x100000) {
            FILE *f = fopen("dump.bin", "wb");
            if (f) { fwrite(G2H(IMG(off)), 1, len, f); fclose(f); log_info("dumped 0x%x+%u", off, len); }
        }
    }
    if (getenv("CODBOZ_SELFTEST")) {
        /* call the patched guest function the way the game does (Thumb, r0 = this, r1 = round) */
        extern uint32_t guest_call2(uint32_t fn, uint32_t a, uint32_t b);
        for (int r = 1; r <= 30; r += r < 10 ? 1 : 10) {
            log_info("selftest zcount(R%d) = %u (expected %d)", r, guest_call2(IMG(0x410900) | 1u, 0, (uint32_t)r),
                     reimagined_zombie_count(r, 1));
        }
    }
}

bool reimagined_deadops(void) {
    uint32_t level = rd(IMG(LEVEL)), waves = rd(IMG(WAVES));
    return !waves && level >= GUEST_HEAP_BASE && level < (uint32_t)GUEST_HEAP_BASE + (uint32_t)GUEST_HEAP_SIZE;
}

const char *reimagined_map(void) {
    return g_map;
}

int reimagined_round(void) {
    return g_round;
}

/* boz-reimagined's readname (0x410d00) reads this file for the profile and co-op name. */
void reimagined_write_name(void) {
    char path[1300];
    snprintf(path, sizeof(path), "%s/save/data-gles1/boz_name.txt", g_user_dir);
    FILE *f = fopen(path, "w");
    if (f) {
        for (const char *p = host_player_name(); *p; ++p) {
            fputc(toupper((unsigned char)*p), f);
        }
        fclose(f);
    }
}

static bool in_match_menu(const char *id) {
    static const char *ingame[] = {"pause", "pausesettings", "sensitivity", "savequit", "leavecoop",
                                   "quitconfirm", "gameover"};
    for (size_t i = 0; id && i < sizeof(ingame) / sizeof(ingame[0]); ++i) {
        if (strcmp(id, ingame[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void read_match(void) {
    uint32_t waves = rd(IMG(WAVES));
    if (waves) {
        g_round = (int)rd(waves + 0x78);
    }
    uint32_t level = rd(IMG(LEVEL));
    if (level && rd(level + 0x54) != 0xffffffffu) {
        const char *name = G2H(level + 0x55);
        if (level >= GUEST_HEAP_BASE) {
            snprintf(g_map, sizeof(g_map), "%.31s", name);
        }
    }
    if (!waves && level) {
        snprintf(g_map, sizeof(g_map), "deadops");
    }
}

static const char *map_title(const char *map) {
    if (strstr(map, "kino") || strstr(map, "theater")) {
        return "KINO DER TOTEN";
    }
    if (strstr(map, "ascension")) {
        return "ASCENSION";
    }
    if (strstr(map, "callofthedead") || strstr(map, "cotd")) {
        return "CALL OF THE DEAD";
    }
    if (strstr(map, "deadops")) {
        return "DEAD OPS ARCADE";
    }
    if (strstr(map, "tutorial")) {
        return "TUTORIAL";
    }
    return map;
}

static void format_time(uint64_t ms, bool hours_always, char *out, size_t size) {
    uint64_t s = ms / 1000;
    if (hours_always || s >= 3600) {
        snprintf(out, size, "%02u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60),
                 (unsigned)(s % 60));
    } else {
        snprintf(out, size, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    }
}

/* Match timer in the game's HUD digits, right-aligned (Hud.drawHudTime). */
static void draw_hud_time(const char *s, float right, float top) {
    const float h = 34, colon = h * 0.34f;
    float width = 0;
    for (const char *p = s; *p; ++p) {
        if (isdigit((unsigned char)*p) && g_digits[*p - '0'].tex) {
            const struct ui_image *d = &g_digits[*p - '0'];
            width += h * (float)d->w / (float)d->h * 0.82f;
        } else if (*p == ':') {
            width += colon;
        }
    }
    float x = right - width;
    for (const char *p = s; *p; ++p) {
        if (isdigit((unsigned char)*p) && g_digits[*p - '0'].tex) {
            const struct ui_image *d = &g_digits[*p - '0'];
            float w = h * (float)d->w / (float)d->h;
            ui_image(d, x, top, w, h, 1.0f);
            x += w * 0.82f;
        } else if (*p == ':') {
            float cx = x + colon * 0.5f, r = h * 0.075f;
            float ys[2] = {top + h * 0.36f, top + h * 0.72f};
            for (int i = 0; i < 2; ++i) {
                float sx = cx - (ys[i] - top) * 0.12f;
                ui_rect(sx - r - 2.5f, ys[i] - r - 2.5f, 2 * r + 5, 2 * r + 5, 0xff111111u);
                ui_rect(sx - r, ys[i] - r, 2 * r, 2 * r, 0xffffffffu);
            }
            x += colon;
        }
    }
}

static int grenade_index(uint32_t item) {
    if (!item) {
        return -1;
    }
    uint32_t h = rd(item + 0x5c);
    return h == FRAG ? 0 : h == MONKEY ? 1 : h == CLAYMORE ? 2 : h == GERSH ? 3
                                                                           : h == MATRYOSHKA ? 4 : -1;
}

static void draw_grenades(void) {
    uint32_t player = rd(IMG(BANK) + 8);
    if (!player) {
        return;
    }
    int counts[ITEMS] = {-1, -1, -1, -1, -1};
    int a = grenade_index(rd(player + 0x1ec)), b = grenade_index(rd(player + 0x1f0));
    int na = (int)rd(player + 0x1cc), nb = (int)rd(player + 0x1d0);
    if (a >= 0 && na >= 0 && na < 100) {
        counts[a] = na;
    }
    if (b >= 0 && nb >= 0 && nb < 100) {
        counts[b] = nb;
    }
    const float hud_x = 1166, row = 54, icon = 44, digit_h = 40;
    float y = 280;
    for (int i = 0; i < ITEMS; ++i) {
        if (counts[i] < 0 || !g_icons[i].tex) {
            continue;
        }
        const struct ui_image *im = &g_icons[i];
        float w = icon * (float)im->w / (float)im->h, h = icon;
        if (w > icon * 1.3f) {
            h = h * icon * 1.3f / w;
            w = icon * 1.3f;
        }
        ui_image(im, hud_x - w / 2, y + (icon - h) / 2, w, h, counts[i] ? 1.0f : 0.45f);
        const struct ui_image *d = &g_digits[counts[i] % 10];
        float dw = digit_h * (float)d->w / (float)d->h;
        ui_image(d, hud_x + 30, y + 2, dw, digit_h, 1.0f);
        y += row;
    }
}

void reimagined_frame(void) {
    if (!g_enabled || !timer_valid()) {
        return;
    }
    const char *screen = screens_current();
    static int last_active = -1;
    if ((int)reimagined_match_active() != last_active) {
        last_active = reimagined_match_active();
        log_info("match %s", last_active ? "active" : "inactive");
    }
    if (getenv("CODBOZ_PERF") && reimagined_match_active()) {
        static uint64_t last;
        uint64_t now = monotonic_ms();
        if (now - last >= 5000) {
            last = now;
            uint32_t t = IMG(TIMER);
            uint64_t elapsed = (uint64_t)rd(t + 12) | ((uint64_t)rd(t + 16) << 32);
            uint32_t net = rd(IMG(0x4958d4));
            if (g_coop_pause.waiting || getenv("CODBOZ_ROUND_LOG")) {
                struct round_snapshot snap;
                round_snapshot(&snap);
                round_log(g_coop_pause.waiting ? "paused" : "live", &snap);
            }
            log_info("perf: wall %llu ms, game timer %llu, round %d, net %08x peers %d", (unsigned long long)now,
                     (unsigned long long)elapsed, g_round, net,
                     net >= (uint32_t)GUEST_HEAP_BASE ? (int)rd(net + 0x64) : -1);
        }
    }
    sync_coop_pause(reimagined_match_active() && screens_coop_session());  /* includes the network check */
    /* debug: CODBOZ_PREVIEW_COOP=1 sets the game's co-op flag, so a solo match opens the native co-op pause */
    if (getenv("CODBOZ_PREVIEW_COOP") && reimagined_match_active() && rd(IMG(LEVEL))) {
        *(volatile uint8_t *)G2H(rd(IMG(LEVEL)) + 0x208) = 1;
    }
    /* debug: CODBOZ_TEST_FREEZE=S holds the pause for 20 s from second S of a match, to measure the freeze */
    if (getenv("CODBOZ_TEST_FREEZE")) {
        static uint64_t since;
        uint64_t now = monotonic_ms();
        uint32_t state = ingame_state();
        if (!reimagined_match_active() || !state) {
            since = 0;
        } else {
            if (!since) {
                since = now;
            }
            uint64_t at = (uint64_t)atoi(getenv("CODBOZ_TEST_FREEZE")) * 1000;
            bool hold = now - since >= at && now - since < at + 20000;
            *(volatile uint8_t *)G2H(state + 0x50) = hold;
            if (hold) {
                g_coop_pause.waiting = true;
            }
            if (hold != g_test_hold) {
                struct round_snapshot snap;
                round_snapshot(&snap);
                round_log(hold ? "test freeze start" : "test freeze end", &snap);
            }
            g_test_hold = hold; /* the card and the low-FPS mode follow, as in a real wait */
        }
    }
    extern void reimagined_coop_stats_frame(void);
    reimagined_coop_stats_frame();
    bool gameplay = reimagined_match_active() && !screen;
    /* the native tick skips time while +48 is set: menus stop the clock */
    *(volatile uint32_t *)G2H(IMG(TIMER) + 48) = gameplay && !g_coop_pause.waiting ? 0u : 1u;
    if (in_match_menu(screen) || gameplay) {
        read_match();
    }
}

/* Co-op "player dropped" card, in the menus' comic style: ink panel, red title strip, paper text. */
static void draw_coop_pause(void) {
    uint64_t now = monotonic_ms();
    const uint32_t paper = 0xfff2eee4u, ink = 0xff151211u, red = 0xff791d24u, edge = 0xff827763u,
                   muted = 0xffbcb8adu;
    ui_rect(0, 0, 1280, 720, 0x99000000u);
    const float x = 330, y = 230, w = 620, h = 250;
    float panel[8] = {x, y + 8, x + w, y, x + w - 4, y + h - 6, x + 3, y + h};
    ui_quad(panel, ink);
    ui_quad_outline(panel, 5, ink);
    ui_quad_outline(panel, 1.5f, edge);
    float strip[8] = {x + 24, y + 30, x + w - 30, y + 22, x + w - 34, y + 84, x + 22, y + 90};
    ui_quad(strip, red);
    ui_font(UI_FONT_DISPLAY);
    ui_label("MATCH PAUSED", 640, y + 72, 40, paper, UI_CENTER);

    /* centred on the text without the dots, so the line doesn't shift as they animate */
    static const char *const dots[4] = {"", ".", "..", "..."};
    const char *waiting = "WAITING FOR A PLAYER TO RECONNECT";
    float left = 640 - ui_text_width(waiting, 24) / 2;
    float end = left + ui_label(waiting, left, y + 138, 24, paper, UI_LEFT);
    ui_label(dots[(now / 400) % 4], end, y + 138, 24, paper, UI_LEFT);

    char line[96];
    int expected = g_coop_pause.baseline >= 2 ? g_coop_pause.baseline : 2;
    int present = g_coop_pause.peers < 1 ? 1 : g_coop_pause.peers;
    if (present >= expected) {
        present = expected - 1;
    }
    snprintf(line, sizeof(line), "PLAYERS %d / %d", present, expected);
    ui_font(UI_FONT_BODY);
    ui_label(line, 640, y + 182, 22, 0xfff5c518u, UI_CENTER);
    ui_label("The match resumes on its own when everyone is back.", 640, y + 220, 18, muted,
             UI_CENTER);
}

/*
 * Scoreboard, in the style of Black Ops 1 (held key; TV: Hud.java, same reads). Data the game keeps:
 *   [0x496110] CScoreManager  +0x40  player name hash -> current points (the HUD number), every player, live
 *   [0x49610c] CStatsManager  +0x10 + 12 * bucket (1 solo, 2 co-op)  stat name hash -> this match, local player
 *   [0x4958d0] CLevelManager  +0x208 co-op, +0x210 local character
 *   0x4a28a8 +4  map of CRemotePlayer (node +0x14). +0x118 -> {player, character}; player +0xc = name
 * A player enters the points map on the first change; until then the game shows the starting 500.
 * Maps are STLport trees: header +4 root; node +8 left, +0xc right, +0x10 key, +0x14 value.
 * The game only sends the others' kills/downs/revives/headshots with the end-of-match results, so
 * during a match they travel over our own channel (coop_stats.c); "-" if that peer was not heard.
 */
enum { SB_SCORES = 0x496110, SB_STATS = 0x49610c, SB_REMOTES = 0x4a28a8, SB_MAX = 4, SB_UNKNOWN = -1, SB_START = 500 };

struct sb_row {
    char name[20];
    int character, score, kills, downs, revives, headshots;
    bool local;
};

static bool g_sb_held;

/* IwHashString: djb2 over the lowercase string */
static uint32_t sb_hash(const char *s) {
    uint32_t h = 5381;
    for (; *s; ++s) {
        h = h * 33 + (uint32_t)tolower((unsigned char)*s);
    }
    return h;
}

/* In-order walk that stays bounded even if the tree is being rebuilt under us. */
static int sb_nodes(uint32_t header, uint32_t *out, int max) {
    uint32_t stack[24], node = header ? rd(header + 4) : 0;
    int depth = 0, count = 0, steps = 0;
    while ((node || depth) && count < max && steps++ < 256) {
        while (node && depth < 24 && steps++ < 256) {
            stack[depth++] = node;
            node = rd(node + 8);
        }
        if (!depth) {
            break;
        }
        node = stack[--depth];
        out[count++] = node;
        node = rd(node + 0xc);
    }
    return count;
}

static int sb_remote_count(void) {
    uint32_t remotes[SB_MAX];
    return sb_nodes(rd(IMG(SB_REMOTES) + 4), remotes, SB_MAX - 1);
}

static int sb_stat(uint32_t header, const char *name) {
    uint32_t nodes[64], key = sb_hash(name);
    int n = sb_nodes(header, nodes, 64);
    for (int i = 0; i < n; ++i) {
        if (rd(nodes[i] + 0x10) == key) {
            return (int)rd(nodes[i] + 0x14);
        }
    }
    return 0;
}

static void sb_upper(char *s) {
    for (; *s; ++s) {
        *s = (char)toupper((unsigned char)*s);
    }
}

static void sb_local_stats(uint32_t stats, bool coop, struct coop_stats *out) {
    uint32_t bucket = rd(stats + (coop ? 2u : 1u) * 0xcu + 0x10);
    out->kills = sb_stat(bucket, "STAT_ZOMBIESKILLED");
    out->downs = sb_stat(bucket, "STAT_PLAYERDEATHS");
    out->revives = sb_stat(bucket, "STAT_REVIVED");
    out->headshots = sb_stat(bucket, "STAT_HEADSHOTS");
}

static int sb_read(struct sb_row *rows) {
    uint32_t scores = rd(IMG(SB_SCORES)), stats = rd(IMG(SB_STATS)), level = rd(IMG(LEVEL));
    if (!scores || !stats || !level) {
        return 0;
    }
    bool coop = (rd(level + 0x208) & 0xffu) != 0;
    uint32_t nodes[16];
    int score_count = sb_nodes(rd(scores + 0x40), nodes, 16), count = 0;
    bool used[16] = {false};
    if (coop) {
        uint32_t remotes[SB_MAX];
        int n = sb_nodes(rd(IMG(SB_REMOTES) + 4), remotes, SB_MAX - 1);
        for (int i = 0; i < n; ++i) {
            uint32_t info = rd(rd(remotes[i] + 0x14) + 0x118), player = rd(info);
            if (!player) {
                continue;
            }
            struct sb_row *r = &rows[count++];
            memset(r, 0, sizeof(*r));
            for (int c = 0; c < 15; ++c) {
                uint32_t a = player + 0xc + (uint32_t)c;
                unsigned char ch = (unsigned char)(rd(a & ~3u) >> ((a & 3u) * 8));
                if (ch < 0x20 || ch > 0x7e) {
                    break;
                }
                r->name[c] = (char)ch;
            }
            r->character = (int)rd(info + 4);
            r->score = SB_START;
            uint32_t key = sb_hash(r->name);
            for (int s = 0; s < score_count; ++s) {
                if (rd(nodes[s] + 0x10) == key) {
                    r->score = (int)rd(nodes[s] + 0x14);
                    used[s] = true;
                }
            }
            struct coop_stats heard;
            if (coop_stats_lookup(key, &heard)) {
                r->kills = heard.kills;
                r->downs = heard.downs;
                r->revives = heard.revives;
                r->headshots = heard.headshots;
            } else {
                r->kills = r->downs = r->revives = r->headshots = SB_UNKNOWN;
            }
            sb_upper(r->name);
        }
    }
    struct sb_row *me = &rows[count++];
    memset(me, 0, sizeof(*me));
    me->local = true;
    snprintf(me->name, sizeof(me->name), "%.15s", host_player_name());
    sb_upper(me->name);
    me->character = (int)rd(level + 0x210);
    me->score = SB_START;
    for (int s = 0; s < score_count; ++s) {
        if (!used[s]) { /* the only one left in solo, whatever the level calls the player */
            me->score = (int)rd(nodes[s] + 0x14);
            break;
        }
    }
    struct coop_stats own;
    sb_local_stats(stats, coop, &own);
    me->kills = own.kills;
    me->downs = own.downs;
    me->revives = own.revives;
    me->headshots = own.headshots;
    return count;
}

/* Co-op, every frame of a match: publish ours, hear theirs, and keep the real party size. */
static void sb_coop_frame(void) {
    uint32_t stats = rd(IMG(SB_STATS)), level = rd(IMG(LEVEL));
    if (!stats || !level || (rd(level + 0x208) & 0xffu) == 0) {
        return;
    }
    struct coop_stats own;
    sb_local_stats(stats, true, &own);
    char name[20];
    snprintf(name, sizeof(name), "%.15s", host_player_name());
    coop_stats_tick(sb_hash(name), &own);
    int party = sb_remote_count() + 1;
    if (party > g_party) {
        g_party = party;
    }
}

static void sb_sort(struct sb_row *rows, int n) {
    for (int i = 1; i < n; ++i) {
        struct sb_row r = rows[i];
        int j = i;
        for (; j > 0 && rows[j - 1].score < r.score; --j) {
            rows[j] = rows[j - 1];
        }
        rows[j] = r;
    }
}

static void sb_value(int value, float x, float y) {
    char text[16];
    if (value == SB_UNKNOWN) {
        snprintf(text, sizeof(text), "-");
    } else {
        snprintf(text, sizeof(text), "%d", value);
    }
    ui_label(text, x, y, 27, 0xfff2eee4u, UI_CENTER);
}

/* Same comic language as the pause card: tilted ink panel, red title strip, paper text. */
static void draw_scoreboard(const struct sb_row *rows, int count, uint64_t elapsed) {
    static const uint32_t colors[4] = {0xfff2eee4u, 0xff74b9e8u, 0xfff5c518u, 0xff86cf72u};
    static const char *const heads[5] = {"SCORE", "KILLS", "DOWNS", "REVIVES", "HEADSHOTS"};
    static const float cols[5] = {560, 680, 790, 900, 1030};
    const uint32_t paper = 0xfff2eee4u, ink = 0xff151211u, red = 0xff791d24u, edge = 0xff827763u,
                   legend = 0xffd5c9acu;
    const float x = 110, w = 1060, row = 62, h = 196 + (float)count * row, y = 360 - h / 2;
    ui_rect(0, 0, 1280, 720, 0x66000000u);
    float panel[8] = {x, y + 8, x + w, y, x + w - 4, y + h - 6, x + 3, y + h};
    ui_quad(panel, 0xf0151211u);
    ui_quad_outline(panel, 5, ink);
    ui_quad_outline(panel, 1.5f, edge);
    float strip[8] = {x + 24, y + 28, x + w - 30, y + 20, x + w - 34, y + 86, x + 22, y + 92};
    ui_quad(strip, red);
    ui_font(UI_FONT_DISPLAY);
    ui_label("SCOREBOARD", x + 52, y + 72, 42, paper, UI_LEFT);
    char line[96];
    if (g_round > 0) {
        snprintf(line, sizeof(line), "%s  -  ROUND %d", map_title(g_map), g_round);
    } else {
        snprintf(line, sizeof(line), "%s", map_title(g_map));
    }
    ui_label(line, x + w - 56, y + 68, 25, paper, UI_RIGHT);
    float head_y = y + 124;
    ui_label("PLAYER", x + 58, head_y, 19, legend, UI_LEFT);
    for (int c = 0; c < 5; ++c) {
        ui_label(heads[c], cols[c], head_y, 19, legend, UI_CENTER);
    }
    float top = y + 136;
    for (int i = 0; i < count; ++i) {
        const struct sb_row *r = &rows[i];
        float ry = top + (float)i * row, lean = i % 2 ? -2.0f : 2.0f;
        if (r->local) {
            float band[8] = {x + 26, ry + 3 + lean, x + w - 28, ry + 3 - lean,
                             x + w - 30, ry + row - 3 - lean, x + 24, ry + row - 3 + lean};
            ui_quad(band, 0xff4a1a1eu);
            ui_quad_outline(band, 1.5f, edge);
        } else {
            ui_rect(x + 34, ry + row - 1, w - 68, 1, 0x66827763u);
        }
        float base = ry + row * 0.5f + 10;
        uint32_t color = colors[(unsigned)r->character & 3u];
        float tag[8] = {x + 40, ry + 15, x + 49, ry + 13, x + 48, ry + row - 15, x + 41, ry + row - 13};
        ui_quad(tag, color);
        ui_label(r->name, x + 58, base, 29, color, UI_LEFT);
        sb_value(r->score, cols[0], base);
        sb_value(r->kills, cols[1], base);
        sb_value(r->downs, cols[2], base);
        sb_value(r->revives, cols[3], base);
        sb_value(r->headshots, cols[4], base);
    }
    char time[32], foot[64];
    format_time(elapsed, false, time, sizeof(time));
    snprintf(foot, sizeof(foot), "MATCH TIME  %s", time);
    ui_label(foot, x + w - 56, y + h - 16, 20, legend, UI_RIGHT);
}

void reimagined_coop_stats_frame(void) {
    if (reimagined_match_active()) {
        sb_coop_frame();
    } else {
        g_party = 0;
    }
}

bool reimagined_scoreboard_available(void) {
    return g_enabled && reimagined_match_active() && !screens_current() && !reimagined_deadops();
}

void reimagined_set_scoreboard(bool held) {
    g_sb_held = held;
}

static void scoreboard_frame(uint64_t elapsed) {
    static int always = -1, preview = -1;
    if (always < 0) {
        always = getenv("CODBOZ_SCOREBOARD_ALWAYS") != NULL; /* debug: real data, no key */
        preview = getenv("CODBOZ_PREVIEW_SCOREBOARD") != NULL; /* debug: four sample rows */
    }
    if (!(g_sb_held || always || preview) || !reimagined_scoreboard_available() || g_coop_pause.waiting) {
        return;
    }
    struct sb_row rows[SB_MAX];
    int count = sb_read(rows);
    if (preview) {
        static const struct sb_row sample[3] = {
            {"RICHTOFEN", 1, 12480, SB_UNKNOWN, SB_UNKNOWN, SB_UNKNOWN, SB_UNKNOWN, false},
            {"TAKEO", 2, 8310, SB_UNKNOWN, SB_UNKNOWN, SB_UNKNOWN, SB_UNKNOWN, false},
            {"NIKOLAI", 3, 4120, SB_UNKNOWN, SB_UNKNOWN, SB_UNKNOWN, SB_UNKNOWN, false},
        };
        count = count > 0 ? 1 : 0;
        for (int i = 0; i < 3; ++i) {
            rows[count++] = sample[i];
        }
    }
    if (count > 0) {
        sb_sort(rows, count);
        draw_scoreboard(rows, count, elapsed);
    }
    if (always && count > 0) {
        static uint64_t last;
        if (monotonic_ms() - last >= 2000) {
            last = monotonic_ms();
            log_info("scoreboard: %s score %d kills %d downs %d revives %d headshots %d (%d row(s))",
                     rows[0].name, rows[0].score, rows[0].kills, rows[0].downs, rows[0].revives,
                     rows[0].headshots, count);
        }
    }
}

void reimagined_draw(void) {
    if (!g_enabled || !timer_valid()) {
        return;
    }
    const char *screen = screens_current();
    uint32_t t = IMG(TIMER);
    bool active = rd(t + 8) == 1;
    uint64_t elapsed = (uint64_t)rd(t + 12) | ((uint64_t)rd(t + 16) << 32);
    uint64_t total = (uint64_t)(rd(t + 20) & 0x7fffffffu) * 1000u + rd(t + 24);
    char text[32];
    static int preview = -1;
    if (preview < 0) {
        const char *env = getenv("CODBOZ_PREVIEW_COOP_PAUSE"); /* debug: show the card in any match */
        preview = env && *env == '1';
        if (preview) {
            g_coop_pause.baseline = 2;
        }
    }
    if (active && !screen && (g_coop_pause.waiting || preview)) {
        draw_coop_pause();
    }
    if (active && !screen) {
        draw_grenades();
        if (g_timer_shown) {
            format_time(elapsed, false, text, sizeof(text));
            draw_hud_time(text, 1232, 150);
        }
        scoreboard_frame(elapsed);
    }
    if (screen && strcmp(screen, "player_profile") == 0) {
        format_time(total, true, text, sizeof(text));
        ui_text("TIME PLAYED:", 1088, 516, 24, 0xfff2eee4u, UI_RIGHT);
        ui_text(text, 1084, 550, 28, 0xfff5c518u, UI_RIGHT);
    }
    extern bool menus_enabled(void);
    if (screen && strcmp(screen, "pause") == 0 && !menus_enabled()) {
        char line[96];
        snprintf(line, sizeof(line), "PLAYER: %s", host_player_name());
        for (char *p = line; *p; ++p) {
            *p = (char)toupper((unsigned char)*p);
        }
        ui_text(line, 640, 300, 30, 0xfff2eee4u, UI_CENTER);
        if (g_map[0]) {
            if (g_round > 0 && strcmp(g_map, "deadops") != 0) {
                snprintf(line, sizeof(line), "%s  -  ROUND %d", map_title(g_map), g_round);
            } else {
                snprintf(line, sizeof(line), "%s", map_title(g_map));
            }
            ui_text(line, 640, 340, 26, 0xfff5c518u, UI_CENTER);
        }
        ui_text(g_timer_shown ? "T: TIMER ON" : "T: TIMER OFF", 640, 690, 20, 0xffd5c9acu,
                UI_CENTER);
    }
}
