/* Hides the virtual stick artwork in the HUD atlas (from cod-boz-port, MIT). */
#include "host.h"

#include <ctype.h>
#include <stdlib.h>
#include <strings.h>

enum {
    HUD_ATLAS_WIDTH = 1024,
    HUD_ATLAS_HEIGHT = 512,
    HUD_ATLAS_BYTES_PER_PIXEL = 2,
};

struct hud_atlas_layout {
    size_t group_size;
    size_t pixel_offset;
};

struct hud_rect {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
};

void codboz_hide_virtual_stick_artwork(const char *name, uint8_t *data, size_t size) {
    static const struct hud_atlas_layout layouts[] = {
        {17446625u, 0x5d898cu},
        {12372273u, 0x41b8e4u},
    };
    static const uint8_t atlas_header[] = {
        0x00, 0x20, 0x00, 0x10, 0x00, 0xbe, 0x32, 0x9b, 0x61, 0x0c, 0x43, 0x24, 0x00, 0x00,
        0x06, 0x00, 0x10, 0x00, 0x10, 0x05, 0x00, 0x10, 0x00, 0x04, 0x00, 0x02, 0x00, 0x08,
    };
    static const struct hud_rect stick_rects[] = {
        {0, 0, 257, 236},
        {257, 0, 92, 72},
        {625, 431, 80, 80},
    };

    const char *slash = strrchr(name ? name : "", '/');
    const char *base = slash ? slash + 1 : (name ? name : "");
    if (strcasecmp(base, "ingame.group.bin") != 0) {
        return;
    }

    size_t atlas_size = HUD_ATLAS_WIDTH * HUD_ATLAS_HEIGHT * HUD_ATLAS_BYTES_PER_PIXEL;
    uint8_t *pixels = NULL;
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); ++i) {
        size_t offset = layouts[i].pixel_offset;
        if (size == layouts[i].group_size && offset >= sizeof(atlas_header) &&
            offset + atlas_size <= size &&
            memcmp(data + offset - sizeof(atlas_header), atlas_header, sizeof(atlas_header)) == 0) {
            pixels = data + offset;
            break;
        }
    }
    if (!pixels) {
        return;
    }

    /* The atlas is RGBA4444; alpha is the low nibble of the first byte. */
    for (size_t i = 0; i < sizeof(stick_rects) / sizeof(stick_rects[0]); ++i) {
        const struct hud_rect *rect = &stick_rects[i];
        for (size_t y = rect->y; y < (size_t)rect->y + rect->height; ++y) {
            uint8_t *row = pixels + (y * HUD_ATLAS_WIDTH + rect->x) * HUD_ATLAS_BYTES_PER_PIXEL;
            for (size_t x = 0; x < rect->width; ++x) {
                row[x * HUD_ATLAS_BYTES_PER_PIXEL] &= 0xf0u;
            }
        }
    }
}

static uint8_t *find_bytes(uint8_t *from, uint8_t *to, const char *needle, size_t len) {
    for (uint8_t *p = from; p && p + len <= to; ++p) {
        if (*p == (uint8_t)needle[0] && memcmp(p, needle, len) == 0) {
            return p;
        }
    }
    return NULL;
}

#define FIND(from, to, lit) find_bytes((from), (to), (lit), sizeof(lit))

static uint32_t djb2_lower(const char *s) {
    uint32_t h = 5381;
    for (; *s; ++s) {
        h = h * 33u + (uint8_t)((*s >= 'A' && *s <= 'Z') ? *s + 32 : *s);
    }
    return h;
}

/*
 * Xperia Play bindings (boz-reimagined build/patch_binds.py): key 126 (reload) is eaten
 * by the global DeviceBack shortcut, so reload moves to key 9, alternateFire (which the
 * patched binary turns into "leave aim") to key 15, and a new key 16 -> toggleFreeMode
 * (the patched binary turns it into "use claymore").
 */
static int patch_xperia_binds(uint8_t **data, uint32_t *size) {
    uint8_t *end = *data + *size;
    uint8_t *a = FIND(*data, end, "XperiaPlayBO");
    uint8_t *b = a ? FIND(a, end, "BOPlayerControlsWin") : NULL;
    if (!a || !b || FIND(a, b, "toggleFreeMode")) {
        return 0;
    }
    uint8_t *reload = FIND(a, b, "+reloadChangeWeapon");
    uint8_t *alt = FIND(a, b, "alternateFire");
    uint8_t *next = FIND(a, b, "changeWeapon");
    uint32_t count;
    memcpy(&count, a + 13, 4);
    if (!reload || !alt || !next || count != 11 || reload[-16] != 0x7e || alt[-15] != 0x09) {
        log_info("unexpected Xperia binding table; controls left unchanged");
        return 0;
    }
    reload[-16] = 0x09;
    alt[-15] = 0x0f;

    size_t rec_len = (size_t)(next - alt);
    uint8_t rec[128];
    if (rec_len < 30 || rec_len + 1 > sizeof(rec)) {
        return 0;
    }
    memcpy(rec, alt - 15, rec_len);
    rec[0] = 0x10;
    uint32_t hash = djb2_lower("toggleFreeMode");
    memcpy(rec + 11, &hash, 4);
    memcpy(rec + 15, "toggleFreeMode", 14);
    memmove(rec + 30, rec + 29, rec_len - 29);
    rec[29] = 0;

    size_t insert_at = (size_t)((next - 15) - *data);
    uint8_t *grown = malloc(*size + rec_len + 1);
    memcpy(grown, *data, insert_at);
    memcpy(grown + insert_at, rec, rec_len + 1);
    memcpy(grown + insert_at + rec_len + 1, *data + insert_at, *size - insert_at);
    count++;
    memcpy(grown + (a - *data) + 13, &count, 4);
    free(*data);
    *data = grown;
    *size += (uint32_t)rec_len + 1;
    return 1;
}

/* patch_pickups_console.py: Call of the Dead leaves the normal lightning-bolt pool. */
static int patch_lightning_pool(uint8_t *data, uint32_t *size) {
    static const char key[] = "LightningBoltLevels";
    static const char old_value[] = "callofthedead|proto";
    uint8_t *k = FIND(data, data + *size, key);
    if (!k || memcmp(k + sizeof(key), old_value, sizeof(old_value)) != 0) {
        return 0;
    }
    uint8_t *value = k + sizeof(key);
    size_t removed = sizeof(old_value) - sizeof("proto");
    memmove(value, value + removed, (size_t)(data + *size - (value + removed)));
    *size -= (uint32_t)removed;
    return 1;
}

int g_reimagined_binds = 1;

void codboz_pc_texts(const char *name, uint8_t *data, uint32_t size);

void codboz_patch_archive_entry(const char *name, uint8_t **data, uint32_t *size) {
    const char *slash = strrchr(name ? name : "", '/');
    const char *base = slash ? slash + 1 : (name ? name : "");
    codboz_hide_virtual_stick_artwork(name, *data, *size);
    codboz_pc_texts(name, *data, *size);
    if (strcasecmp(base, "console.bin") != 0 || !g_reimagined_binds) {
        return;
    }
    if (patch_xperia_binds(data, size)) {
        log_info("%s: Xperia bindings updated (reload, aim, claymore)", name);
    }
    if (patch_lightning_pool(*data, size)) {
        log_debug("%s: lightning bolt pool updated", name);
    }
}

/* ---- PC prompts instead of the touch screen's and the TV's ---- */

/* First single-character keyboard binding of an action, else the fallback. */
static char key_letter(int action, char fallback) {
    extern const char *bindings_get(int action, int slot);
    for (int s = 0; s < 4; ++s) {
        const char *in = bindings_get(action, s);
        if (strncmp(in, "KEY:", 4) == 0 && in[4] && !in[5]) {
            return in[4];
        }
    }
    return fallback;
}

/* Replace every occurrence with a same-size text, centred with spaces. */
static int replace_same_size(uint8_t *data, uint32_t size, const char *old_text, const char *new_text) {
    size_t n = strlen(old_text), m = strlen(new_text);
    int count = 0;
    if (m > n) {
        return 0;
    }
    for (uint8_t *p = data; p + n <= data + size;) {
        uint8_t *hit = memchr(p, (unsigned char)old_text[0], (size_t)(data + size - p));
        if (!hit || hit + n > data + size) {
            break;
        }
        if (memcmp(hit, old_text, n) == 0) {
            size_t left = (n - m) / 2;
            memset(hit, ' ', n);
            memcpy(hit + left, new_text, m);
            count++;
            p = hit + n;
        } else {
            p = hit + 1;
        }
    }
    return count;
}

/* "Tap to X" / "[A] to X" -> "[E] to X" (same length), at the start of a string. */
static int retag_prompts(uint8_t *data, uint32_t size, char key) {
    static const char *const heads[] = {"Tap to ", "TAP TO ", "tap to ", "[A] to ", "[A] TO ",
                                        "[A] "};
    int count = 0;
    for (size_t h = 0; h < sizeof(heads) / sizeof(heads[0]); ++h) {
        size_t n = strlen(heads[h]);
        for (uint8_t *p = data; p + n <= data + size; ++p) {
            if ((p == data || p[-1] == 0) && memcmp(p, heads[h], n) == 0) {
                p[0] = '[';
                p[1] = (uint8_t)toupper((unsigned char)key);
                p[2] = ']';
                count++;
            }
        }
    }
    return count;
}

void codboz_pc_texts(const char *name, uint8_t *data, uint32_t size) {
    char use = key_letter(7 /* BIND_USE */, 'E');
    char swap = key_letter(13 /* BIND_SWITCH */, '1');
    char swap_text[40];
    snprintf(swap_text, sizeof(swap_text), "Press [%c] to swap your weapon.", toupper((unsigned char)swap));
    int n = 0;
    static const char *const starts[] = {"Touch Screen to Start", "Press A to Start", "Press B to Start"};
    static const char *const starts_lower[] = {"touch screen to start", "press a to start",
                                               "press b to start"};
    for (int i = 0; i < 3; ++i) {
        n += replace_same_size(data, size, starts[i], "Click to Start");
        n += replace_same_size(data, size, starts_lower[i], "click to start");
    }
    n += replace_same_size(data, size, "Double tap to swap your weapon.", swap_text);
    n += replace_same_size(data, size, "Press [Y] to swap your weapon.", swap_text);
    n += retag_prompts(data, size, use);
    if (n) {
        log_info("%s: %d prompts set for keyboard and mouse", name, n);
    }
}
