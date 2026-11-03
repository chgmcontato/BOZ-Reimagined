#include "bindings.h"
#include "host.h"

#include <SDL.h>
#include <ctype.h>
#include <strings.h>

static char g_map[BIND_COUNT][BIND_SLOTS][32];
static int g_wheel_up, g_wheel_down;

static const char *const k_ids[BIND_COUNT] = {
    "forward", "back",    "left",     "right",    "shoot",  "aim",    "reload", "use",
    "melee",   "grenade", "tactical", "claymore", "crouch", "switch", "pause",
    "nuke",    "boost",   "scoreboard",
};

static const char *const k_labels[BIND_COUNT] = {
    "Andar para frente", "Andar para trás", "Andar para a esquerda", "Andar para a direita",
    "Atirar", "Mirar (segurar)", "Recarregar", "Usar / comprar / correr", "Faca",
    "Granada", "Tático (Monkey Bomb)", "Claymore", "Agachar", "Trocar de arma", "Pausar",
    "Nuke (Dead Ops Arcade)", "Boost (Dead Ops Arcade)", "Placar (Reimagined)",
};

/* Keyboard + mouse defaults, then the Black Ops 1 console layout. */
static const char *const k_defaults[BIND_COUNT][BIND_SLOTS] = {
    {"KEY:W", "KEY:Up", NULL, NULL},
    {"KEY:S", "KEY:Down", NULL, NULL},
    {"KEY:A", "KEY:Left", NULL, NULL},
    {"KEY:D", "KEY:Right", NULL, NULL},
    {"MOUSE:1", "PADAXIS:righttrigger", NULL, NULL},
    {"MOUSE:3", "PADAXIS:lefttrigger", NULL, NULL},
    {"KEY:R", "PAD:x", NULL, NULL},
    {"KEY:E", "KEY:Left Shift", "KEY:Space", "PAD:a"},
    {"KEY:V", "MOUSE:2", "PAD:rightstick", NULL},
    {"KEY:G", "MOUSE:5", "PAD:rightshoulder", NULL},
    {"KEY:Q", "PAD:leftshoulder", NULL, NULL},
    {"KEY:4", "PAD:dpright", NULL, NULL},
    {"KEY:C", "KEY:Left Ctrl", "PAD:b", NULL},
    {"KEY:1", "WHEEL:UP", "WHEEL:DOWN", "PAD:y"},
    {"KEY:Escape", "PAD:start", NULL, NULL},
    {"KEY:X", "MOUSE:2", "PADAXIS:righttrigger", NULL},
    {"MOUSE:3", "KEY:Z", "PADAXIS:lefttrigger", NULL},
    {"KEY:Tab", "PAD:back", NULL, NULL},
};

const char *bindings_id(enum bind_action a) {
    return k_ids[a];
}

static const char *const k_labels_en[BIND_COUNT] = {
    "Move forward", "Move back", "Move left", "Move right", "Shoot", "Aim (hold)", "Reload",
    "Use / buy / sprint", "Melee", "Grenade", "Tactical (Monkey Bomb)", "Claymore", "Crouch",
    "Switch weapon", "Pause", "Nuke (Dead Ops Arcade)", "Boost (Dead Ops Arcade)",
    "Scoreboard (Reimagined)",
};

const char *bindings_label(enum bind_action a) {
    return g_opt.english ? k_labels_en[a] : k_labels[a];
}

const char *bindings_get(enum bind_action a, int slot) {
    return g_map[a][slot];
}

void bindings_set(enum bind_action a, int slot, const char *input) {
    snprintf(g_map[a][slot], sizeof(g_map[a][slot]), "%s", input ? input : "");
}

void bindings_reset(void) {
    for (int a = 0; a < BIND_COUNT; ++a) {
        for (int s = 0; s < BIND_SLOTS; ++s) {
            bindings_set((enum bind_action)a, s, k_defaults[a][s]);
        }
    }
}

bool bindings_load(const char *path) {
    bindings_reset();
    FILE *f = fopen(path, "r");
    if (!f) {
        bindings_save(path);
        return false;
    }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n#")] = 0;
        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = 0;
        char *key = line;
        while (isspace((unsigned char)*key)) {
            key++;
        }
        for (char *e = eq - 1; e >= key && isspace((unsigned char)*e); --e) {
            *e = 0;
        }
        for (int a = 0; a < BIND_COUNT; ++a) {
            if (strcasecmp(key, k_ids[a]) != 0) {
                continue;
            }
            int slot = 0;
            for (char *tok = strtok(eq + 1, ","); tok && slot < BIND_SLOTS; tok = strtok(NULL, ",")) {
                while (isspace((unsigned char)*tok)) {
                    tok++;
                }
                for (char *e = tok + strlen(tok) - 1; e >= tok && isspace((unsigned char)*e); --e) {
                    *e = 0;
                }
                bindings_set((enum bind_action)a, slot++, tok);
            }
            while (slot < BIND_SLOTS) {
                bindings_set((enum bind_action)a, slot++, "");
            }
        }
    }
    fclose(f);
    return true;
}

bool bindings_save(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        return false;
    }
    fputs("# action = input, input, ...   (KEY:<name>, MOUSE:1-5, WHEEL:UP/DOWN, PAD:<button>, "
          "PADAXIS:lefttrigger/righttrigger)\n",
          f);
    for (int a = 0; a < BIND_COUNT; ++a) {
        fprintf(f, "%s = ", k_ids[a]);
        int first = 1;
        for (int s = 0; s < BIND_SLOTS; ++s) {
            if (g_map[a][s][0]) {
                fprintf(f, "%s%s", first ? "" : ", ", g_map[a][s]);
                first = 0;
            }
        }
        fputc('\n', f);
    }
    fclose(f);
    return true;
}

void bindings_describe(const char *input, char *out, int size) {
    if (!input || !input[0]) {
        snprintf(out, (size_t)size, "-");
    } else if (strncmp(input, "KEY:", 4) == 0) {
        snprintf(out, (size_t)size, "%s", input + 4);
    } else if (strncmp(input, "MOUSE:", 6) == 0) {
        static const char *names[] = {"", "Mouse esq.", "Mouse meio", "Mouse dir.", "Mouse 4",
                                      "Mouse 5"};
        static const char *names_en[] = {"", "Left mouse", "Middle mouse", "Right mouse",
                                         "Mouse 4", "Mouse 5"};
        int b = atoi(input + 6);
        snprintf(out, (size_t)size, "%s", b >= 1 && b <= 5 ? (g_opt.english ? names_en : names)[b] : input);
    } else if (strncmp(input, "WHEEL:", 6) == 0) {
        bool up = strcmp(input + 6, "UP") == 0;
        snprintf(out, (size_t)size, "%s", g_opt.english ? (up ? "Wheel up" : "Wheel down")
                                                        : (up ? "Roda cima" : "Roda baixo"));
    } else if (strncmp(input, "PAD:", 4) == 0) {
        /* Xbox labels */
        static const char *const names[][2] = {
            {"a", "A"},           {"b", "B"},           {"x", "X"},
            {"y", "Y"},           {"leftshoulder", "LB"}, {"rightshoulder", "RB"},
            {"leftstick", "LS"},  {"rightstick", "RS"}, {"start", "Start"},
            {"back", "Back"},     {"guide", "Guide"},   {"dpup", "D-pad cima"},
            {"dpdown", "D-pad baixo"}, {"dpleft", "D-pad esq."}, {"dpright", "D-pad dir."},
        };
        const char *shown = input + 4;
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
            if (strcmp(input + 4, names[i][0]) == 0) {
                shown = names[i][1];
            }
        }
        snprintf(out, (size_t)size, "%s %s", g_opt.english ? "Pad" : "Controle", shown);
    } else if (strncmp(input, "PADAXIS:", 8) == 0) {
        snprintf(out, (size_t)size, "%s %s", g_opt.english ? "Pad" : "Controle",
                 strcmp(input + 8, "righttrigger") == 0 ? "RT" : "LT");
    } else {
        snprintf(out, (size_t)size, "%s", input);
    }
}

void bindings_wheel(int y) {
    if (y > 0) {
        g_wheel_up = 4;
    } else if (y < 0) {
        g_wheel_down = 4;
    }
}

void bindings_end_frame(void) {
    if (g_wheel_up > 0) {
        g_wheel_up--;
    }
    if (g_wheel_down > 0) {
        g_wheel_down--;
    }
}

static bool input_down(const char *in, bool km, SDL_GameController *pad) {
    if (!in[0]) {
        return false;
    }
    if (km && strncmp(in, "KEY:", 4) == 0) {
        SDL_Scancode sc = SDL_GetScancodeFromName(in + 4);
        return sc != SDL_SCANCODE_UNKNOWN && SDL_GetKeyboardState(NULL)[sc];
    }
    if (km && strncmp(in, "MOUSE:", 6) == 0) {
        int b = atoi(in + 6);
#ifdef CODBOZ_GLES
        /* SDL copies every finger to the left mouse button: a touch anywhere would fire */
        return false;
#endif
        return b >= 1 && b <= 5 && (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(b));
    }
    if (km && strncmp(in, "WHEEL:", 6) == 0) {
        return strcmp(in + 6, "UP") == 0 ? g_wheel_up > 1 : g_wheel_down > 1;
    }
    if (pad && strncmp(in, "PAD:", 4) == 0) {
        SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(in + 4);
        return b != SDL_CONTROLLER_BUTTON_INVALID && SDL_GameControllerGetButton(pad, b);
    }
    if (pad && strncmp(in, "PADAXIS:", 8) == 0) {
        SDL_GameControllerAxis ax = SDL_GameControllerGetAxisFromString(in + 8);
        return ax != SDL_CONTROLLER_AXIS_INVALID && SDL_GameControllerGetAxis(pad, ax) > 12000;
    }
    return false;
}

bool bindings_uses(enum bind_action a, const char *input) {
    for (int s = 0; s < BIND_SLOTS; ++s) {
        if (strcasecmp(g_map[a][s], input) == 0) {
            return true;
        }
    }
    return false;
}

bool bindings_down(enum bind_action a, bool keyboard_mouse, void *controller) {
    for (int s = 0; s < BIND_SLOTS; ++s) {
        if (input_down(g_map[a][s], keyboard_mouse, controller)) {
            return true;
        }
    }
    return false;
}
