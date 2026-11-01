/* The image's embedded .icf configuration plus the PC launcher settings. */
#include "host.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <strings.h>

enum { CONFIG_MAX_ENTRIES = 768 };

struct config_entry {
    char section[64];
    char key[96];
    char value[256];
};

static struct config_entry g_entries[CONFIG_MAX_ENTRIES];
static size_t g_entry_count;
static char g_player_name[14] = "Player";

struct host_options g_opt = {
    .width = 1280,
    .height = 720,
    .fullscreen = true,
    .vsync = true,
    .fps_limit = 0, /* unlimited: the game's own 30 fps cap is patched out (main.c) */
    .show_fps = false,
    .mouse_sensitivity = 1.0f,
    .invert_y = false,
    .mouse_drag = false,
};

static char *trim(char *text) {
    while (*text && isspace((unsigned char)*text)) {
        text++;
    }
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        *--end = 0;
    }
    return text;
}

static void strip_comment(char *line) {
    int in_quote = 0;
    for (char *p = line; *p; ++p) {
        if (*p == '"') {
            in_quote = !in_quote;
        } else if (*p == '#' && !in_quote) {
            *p = 0;
            return;
        }
    }
}

static void strip_quotes(char *value) {
    size_t len = strlen(value);
    if (len >= 2 && value[0] == '"' && value[len - 1] == '"') {
        memmove(value, value + 1, len - 2);
        value[len - 2] = 0;
    }
}

static int config_find(const char *section, const char *key) {
    for (size_t i = 0; i < g_entry_count; ++i) {
        if (strcasecmp(g_entries[i].section, section) == 0 &&
            strcasecmp(g_entries[i].key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

const char *config_get(const char *section, const char *key) {
    if (!section || !key) {
        return NULL;
    }
    int index = config_find(section, key);
    return index >= 0 ? g_entries[index].value : NULL;
}

void config_set(const char *section, const char *key, const char *value) {
    if (!section || !key || !section[0] || !key[0]) {
        return;
    }
    int index = config_find(section, key);
    if (index < 0) {
        if (g_entry_count >= CONFIG_MAX_ENTRIES) {
            return;
        }
        index = (int)g_entry_count++;
    }
    snprintf(g_entries[index].section, sizeof(g_entries[index].section), "%s", section);
    snprintf(g_entries[index].key, sizeof(g_entries[index].key), "%s", key);
    snprintf(g_entries[index].value, sizeof(g_entries[index].value), "%s", value ? value : "");
}

static int contains_nocase(const char *text, const char *needle) {
    size_t n = strlen(needle);
    for (const char *p = text; *p; ++p) {
        if (strncasecmp(p, needle, n) == 0) {
            return 1;
        }
    }
    return n == 0;
}

/* We present ourselves as an Android Xperia Play, the device with physical controls. */
static int platform_matches(const char *value) {
    return contains_nocase(value, "ANY") || contains_nocase(value, "ANDROID");
}

static int id_matches(const char *value) {
    if (contains_nocase(value, "ANY")) {
        return 1;
    }
    if (!platform_matches(value)) {
        return 0;
    }
    if (!strchr(value, '"')) {
        return 1;
    }
    return contains_nocase(value, "R800i") || contains_nocase(value, "Sony Ericsson Xperia Play");
}

static int condition_matches(const char *condition) {
    char copy[256];
    snprintf(copy, sizeof(copy), "%s", condition ? condition : "");
    char *expr = trim(copy);
    if (!expr[0]) {
        return 1;
    }
    if (strncasecmp(expr, "OS=", 3) == 0) {
        return platform_matches(expr + 3);
    }
    if (strncasecmp(expr, "ID=", 3) == 0 || strncasecmp(expr, "CLASS=", 6) == 0) {
        char *equals = strchr(expr, '=');
        return equals && id_matches(trim(equals + 1));
    }
    if (expr[0] == '[') {
        char *section_end = strchr(expr, ']');
        char *equals = strstr(expr, "==");
        if (!section_end || !equals || equals <= section_end) {
            return 0;
        }
        *section_end = 0;
        *equals = 0;
        char *value = trim(equals + 2);
        strip_quotes(value);
        const char *current = config_get(trim(expr + 1), trim(section_end + 1));
        return current && strcasecmp(current, value) == 0;
    }
    return 0;
}

static int parse_int_value(const char *value, int depth, int32_t *out) {
    if (!value || depth > 4) {
        return 0;
    }
    char copy[256];
    snprintf(copy, sizeof(copy), "%s", value);
    char *text = trim(copy);
    if (!text[0]) {
        return 0;
    }
    if (text[0] == '[') {
        char *section_end = strchr(text, ']');
        if (!section_end) {
            return 0;
        }
        *section_end = 0;
        char *key = section_end + 1;
        char *op = strpbrk(key, "+-");
        char oper = 0;
        int32_t adjustment = 0;
        if (op) {
            oper = *op;
            *op = 0;
            adjustment = (int32_t)strtol(trim(op + 1), NULL, 0);
        }
        int32_t base = 0;
        if (!parse_int_value(config_get(trim(text + 1), trim(key)), depth + 1, &base)) {
            return 0;
        }
        *out = oper == '+' ? base + adjustment : oper == '-' ? base - adjustment : base;
        return 1;
    }
    char *end = NULL;
    long parsed = strtol(text, &end, 0);
    if (end == text) {
        return 0;
    }
    *out = (int32_t)parsed;
    return 1;
}

static bool valid_player_name(const char *value);

void settings_save(void) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/settings.txt", g_data_root);
    FILE *file = fopen(path, "w");
    if (!file) {
        return;
    }
    fprintf(file,
            "# Call of Duty: Black Ops Zombies - PC settings\n"
            "# fullscreen=1: borderless at the monitor's resolution (width/height ignored)\n"
            "fullscreen=%d\nwidth=%d\nheight=%d\nvsync=%d\nfps_limit=%d\nshow_fps=%d\n"
            "mouse_sensitivity=%.2f\ninvert_y=%d\nmouse_look=%s\nlanguage=%s\nplayer_name=%s\n",
            g_opt.fullscreen, g_opt.width, g_opt.height, g_opt.vsync, g_opt.fps_limit,
            g_opt.show_fps, g_opt.mouse_sensitivity, g_opt.invert_y,
            g_opt.mouse_drag ? "drag" : "stick", g_opt.english ? "en" : "pt", g_player_name);
    fclose(file);
}

void settings_set_player_name(const char *name) {
    if (valid_player_name(name)) {
        snprintf(g_player_name, sizeof(g_player_name), "%s", name);
        settings_save();
    }
}

static int asset_exists(const char *name) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/assets/%s", g_root, name);
    FILE *file = fopen(path, "rb");
    if (file) {
        fclose(file);
        return 1;
    }
    return 0;
}

static bool parse_bool(const char *value) {
    return strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0 ||
           strcasecmp(value, "yes") == 0 || strcasecmp(value, "on") == 0;
}

static bool valid_player_name(const char *value) {
    size_t length = strlen(value);
    if (length == 0 || length >= sizeof(g_player_name)) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (!isalnum(c) && c != ' ' && c != '-' && c != '_' && c != '.') {
            return false;
        }
    }
    return true;
}

static const char k_default_settings[] =
    "# Call of Duty: Black Ops Zombies - PC settings\n"
    "width=1280\n"
    "height=720\n"
    "# fullscreen=1: borderless at the monitor's resolution (width/height ignored)\n"
    "fullscreen=1\n"
    "vsync=1\n"
    "fps_limit=0\n"
    "show_fps=0\n"
    "mouse_sensitivity=1.0\n"
    "invert_y=0\n"
    "player_name=Player\n";

void settings_load(void) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/settings.txt", g_data_root);
    FILE *file = fopen(path, "r");
    if (!file) {
        file = fopen(path, "w");
        if (file) {
            fputs(k_default_settings, file);
            fclose(file);
        }
        file = fopen(path, "r");
        if (!file) {
            return;
        }
    }
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = 0;
        strip_comment(line);
        char *equals = strchr(line, '=');
        if (!equals) {
            continue;
        }
        *equals = 0;
        char *key = trim(line);
        char *value = trim(equals + 1);
        strip_quotes(value);
        if (strcmp(key, "width") == 0) {
            g_opt.width = atoi(value);
        } else if (strcmp(key, "height") == 0) {
            g_opt.height = atoi(value);
        } else if (strcmp(key, "fullscreen") == 0) {
            g_opt.fullscreen = parse_bool(value);
        } else if (strcmp(key, "vsync") == 0) {
            g_opt.vsync = parse_bool(value);
        } else if (strcmp(key, "fps_limit") == 0) {
            g_opt.fps_limit = atoi(value);
        } else if (strcmp(key, "show_fps") == 0) {
            g_opt.show_fps = parse_bool(value);
        } else if (strcmp(key, "language") == 0) {
            g_opt.english = strcmp(value, "en") == 0;
        } else if (strcmp(key, "mouse_look") == 0) {
            g_opt.mouse_drag = strcmp(value, "stick") != 0;
        } else if (strcmp(key, "mouse_sensitivity") == 0) {
            g_opt.mouse_sensitivity = (float)atof(value);
        } else if (strcmp(key, "invert_y") == 0) {
            g_opt.invert_y = parse_bool(value);
        } else if (strcmp(key, "player_name") == 0 && valid_player_name(value)) {
            snprintf(g_player_name, sizeof(g_player_name), "%s", value);
        }
    }
    fclose(file);
    if (g_opt.width < 320 || g_opt.height < 240) {
        g_opt.width = 1280;
        g_opt.height = 720;
    }
    if (g_opt.mouse_sensitivity <= 0.0f) {
        g_opt.mouse_sensitivity = 1.0f;
    }
}

const char *host_player_name(void) {
    return g_player_name;
}

void config_load_image(const uint8_t *data, uint32_t size) {
    g_entry_count = 0;
    char *text = malloc((size_t)size + 1);
    memcpy(text, data, size);
    text[size] = 0;
    char section[64] = "";
    int active = 1;
    char *cursor = text;
    while (cursor && *cursor) {
        char *line = cursor;
        cursor = strchr(cursor, '\n');
        if (cursor) {
            *cursor++ = 0;
        }
        line[strcspn(line, "\r")] = 0;
        strip_comment(line);
        char *clean = trim(line);
        size_t len = strlen(clean);
        if (!len) {
            continue;
        }
        if (clean[0] == '{' && clean[len - 1] == '}') {
            clean[len - 1] = 0;
            active = condition_matches(clean + 1);
            continue;
        }
        if (clean[0] == '[' && clean[len - 1] == ']') {
            clean[len - 1] = 0;
            snprintf(section, sizeof(section), "%s", trim(clean + 1));
            continue;
        }
        char *equals = strchr(clean, '=');
        if (!active || !equals) {
            continue;
        }
        *equals = 0;
        char *value = trim(equals + 1);
        strip_quotes(value);
        config_set(section, trim(clean), value);
    }
    free(text);

    /* Same overrides as the handheld port: offline, generous budgets, no store. */
    config_set("GX", "NumTPages", "512");
    config_set("GX", "NumTPagesNoMipMap", "256");
    config_set("GX", "NumTPageFreeRects", "8192");
    config_set("GX", "MaxTexturesPerTPage", "512");
    config_set("GAME", "ResourceDownloader", "0");
    config_set("GAME", "EnableGC", "0");
    config_set("GAME", "EnableAndroidMarketBilling", "0");
    config_set("GAME", "VoiceChatEnabled", "0");
    config_set("GAME", "LowEndDevice", "0");
    config_set("GAME", "LowMemoryDevice", "0");
    config_set("GAME", "GuiBucketSize", "2500000");
    config_set("GAME", "FrontendMemoryWarningLevel", "0");
    config_set("GAME", "OnlineAccount", "NONE");
    config_set("GAME", "OnlineUseGameCenterMM", "0");
    config_set("GAME", "MatchmakingSearchAndPublishMode", "0");
    config_set("Demonware", "OnlineAccount", "NONE");
    config_set("Demonware", "LSGServer", "");
    config_set("Demonware", "AuthServer", "");
    config_set("Demonware", "STUNServer", "");
    config_set("ONLINE", "dispatcher", "");
    config_set("FLASH", "FlashBucketSize", "32000000");
    config_set("FLASH", "FlashBucketHeapAnalyse", "0");
    /* The archive's name says which texture build it holds (Android OBB: etc; iOS: gles1). */
    const char *style;
    if (g_obb_path[0]) {
        const char *name = strrchr(g_obb_path, '/');
        name = name ? name + 1 : g_obb_path;
        style = strstr(name, "gles1") ? "gles1" : strstr(name, "dxt") ? "dxt" : "etc";
    } else {
        style = asset_exists("blackops_etc.dz")   ? "etc"
                : asset_exists("blackops_dxt.dz") ? "dxt"
                                                  : "gles1";
    }
    config_set("RESMANAGER", "ResBuildStyle", style);
    g_pvrtc_textures = strcmp(style, "gles1") == 0;
}

bool g_pvrtc_textures;

IMPORT(s3eConfigGetInt) {
    const char *value = config_get(G2H(arg(0)), G2H(arg(1)));
    int32_t parsed = 0;
    int found = value && parse_int_value(value, 0, &parsed);
    if (found && arg(2)) {
        *(int32_t *)G2H(arg(2)) = parsed;
    }
    ret(found ? 0 : 1);
}

IMPORT(s3eConfigGetString) {
    const char *value = config_get(G2H(arg(0)), G2H(arg(1)));
    if (!value || !arg(2)) {
        ret(1);
        return;
    }
    /* S3E_CONFIG_STRING_MAX */
    snprintf(G2H(arg(2)), 256, "%s", value);
    ret(0);
}

void register_config_imports(void) {
    REG(s3eConfigGetInt);
    REG(s3eConfigGetString);
}
