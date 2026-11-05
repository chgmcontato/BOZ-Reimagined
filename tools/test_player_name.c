/* Default player name: "Player" (old default, or a first run) becomes Player-XXXX once and stays;
 * a name the player chose is kept. Uses a temporary data folder, no game data. */
#include <assert.h>
#include <unistd.h>

#include "../src/config.c"

char g_data_root[1024];
char g_root[1024];
char g_obb_path[1024];
int g_log_verbose;

/* config.c also holds the s3eConfig imports; nothing here calls them */
uint32_t arg(int index) {
    (void)index;
    return 0;
}
void ret(uint32_t value) {
    (void)value;
}
void import_register(const char *name, import_fn fn) {
    (void)name;
    (void)fn;
}

uint64_t monotonic_us(void) {
    static uint64_t now = 1000000;
    return now += 7919;
}

static void write_settings(const char *name) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/settings.txt", g_data_root);
    FILE *file = fopen(path, "w");
    assert(file);
    fprintf(file, "fullscreen=0\nplayer_name=%s\n", name);
    fclose(file);
}

static void reload(void) {
    snprintf(g_player_name, sizeof(g_player_name), "%s", k_legacy_default_name);
    settings_load();
}

static bool generated(const char *name) {
    if (strlen(name) != 11 || strncmp(name, "Player-", 7) != 0 || !valid_player_name(name)) {
        return false;
    }
    for (int i = 7; i < 11; ++i) {
        if (!strchr("23456789ABCDEFGHJKLMNPQRSTUVWXYZ", name[i])) {
            return false;
        }
    }
    return true;
}

int main(void) {
    char dir[] = "/tmp/codboz-player-name-XXXXXX";
    assert(mkdtemp(dir));
    snprintf(g_data_root, sizeof(g_data_root), "%s", dir);

    /* first run: no settings.txt yet */
    reload();
    char first[16];
    snprintf(first, sizeof(first), "%s", host_player_name());
    assert(generated(first));

    /* the next start reads the saved name back */
    reload();
    assert(strcmp(host_player_name(), first) == 0);

    /* the old default saved by an earlier version is replaced, once */
    write_settings("Player");
    reload();
    char second[16];
    snprintf(second, sizeof(second), "%s", host_player_name());
    assert(generated(second));
    reload();
    assert(strcmp(host_player_name(), second) == 0);

    /* a name the player typed stays as it is */
    write_settings("HEFESTO");
    reload();
    assert(strcmp(host_player_name(), "HEFESTO") == 0);
    write_settings("Player 2");
    reload();
    assert(strcmp(host_player_name(), "Player 2") == 0);

    char path[1200];
    snprintf(path, sizeof(path), "%s/settings.txt", dir);
    unlink(path);
    rmdir(dir);
    printf("player name: default %s, then %s; chosen names kept\n", first, second);
    return 0;
}
