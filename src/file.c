/*
 * s3eFile: game data comes from <root>/assets and the DTRZ archive (the OBB);
 * anything the game writes goes to the user directory.
 */
#include "host.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define mkdir_one(path) _mkdir(path)
#define access _access
#define F_OK 0
#else
#include <unistd.h>
#define mkdir_one(path) mkdir(path, 0777)
#endif

enum {
    MAX_FILES = 256,
    DTRZ_MAX_ENTRIES = 1024,
    DTRZ_NAME_MAX = 192,
};

struct dtrz_entry {
    char name[DTRZ_NAME_MAX];
    char lower_name[DTRZ_NAME_MAX];
    char lower_base[DTRZ_NAME_MAX];
    uint32_t offset;
    uint32_t size;
};

struct dtrz_index {
    int loaded;
    size_t count;
    char path[1200];
    struct dtrz_entry entries[DTRZ_MAX_ENTRIES];
};

/* Either a real file or an in-memory copy of an archive entry. */
struct vfile {
    int used;
    FILE *file;
    uint8_t *mem;
    uint32_t mem_size;
    uint32_t mem_pos;
    int owns_mem;
    int eof;
};

static struct dtrz_index g_dtrz;
static FILE *g_dtrz_fp = NULL; /* Keep the archive open across asset reads. */
static struct vfile g_files[MAX_FILES];
static uint32_t g_handle_base; /* guest-visible handle tokens */
static int32_t g_file_error;

void codboz_patch_archive_entry(const char *name, uint8_t **data, uint32_t *size);

static uint32_t handle_of(int index) {
    if (!g_handle_base) {
        g_handle_base = H2G(arena_alloc(MAX_FILES * 16));
    }
    return g_handle_base + (uint32_t)index * 16u;
}

static struct vfile *vfile_of(uint32_t handle) {
    if (!g_handle_base || handle < g_handle_base || handle >= g_handle_base + MAX_FILES * 16 ||
        (handle - g_handle_base) % 16) {
        return NULL;
    }
    struct vfile *f = &g_files[(handle - g_handle_base) / 16];
    return f->used ? f : NULL;
}

static uint32_t vfile_new(FILE *file, uint8_t *mem, uint32_t size, int owns) {
    for (int i = 0; i < MAX_FILES; ++i) {
        if (!g_files[i].used) {
            g_files[i] = (struct vfile){1, file, mem, size, 0, owns, 0};
            return handle_of(i);
        }
    }
    if (file) {
        fclose(file);
    }
    if (owns) {
        free(mem);
    }
    log_error("too many open files");
    return 0;
}

static int path_exists(const char *path) {
    return access(path, F_OK) == 0;
}

static const char *base_name(const char *name) {
    const char *slash = strrchr(name ? name : "", '/');
    return slash ? slash + 1 : (name ? name : "");
}

static void lower_copy(char *out, size_t out_size, const char *in) {
    size_t i = 0;
    for (; in && in[i] && i + 1 < out_size; ++i) {
        out[i] = (char)tolower((unsigned char)in[i]);
    }
    out[i] = 0;
}

static void make_parent_dirs(const char *path) {
    char tmp[1200];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = 0;
            mkdir_one(tmp);
            *p = saved;
        }
    }
}

/* Marmalade paths may carry a drive prefix ("rom://", "ram://", "raw://"). */
static const char *strip_drive(const char *name) {
    const char *sep = strstr(name, "://");
    if (sep && sep - name <= 5) {
        return sep + 3;
    }
    while (*name == '/' || *name == '\\') {
        name++;
    }
    return name;
}

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int read_c_string(FILE *file, char *out, size_t out_size) {
    size_t len = 0;
    int ch;
    while ((ch = fgetc(file)) != EOF) {
        if (ch == 0) {
            out[len] = 0;
            return 1;
        }
        if (len + 1 < out_size) {
            out[len++] = (char)ch;
        }
    }
    out[len] = 0;
    return 0;
}

static int dtrz_load_index(void) {
    if (g_dtrz.loaded) {
        return g_dtrz.count > 0;
    }
    g_dtrz.loaded = 1;
    if (g_obb_path[0] && path_exists(g_obb_path)) {
        snprintf(g_dtrz.path, sizeof(g_dtrz.path), "%s", g_obb_path);
    }
    static const char *archives[] = {"blackops_etc.dz", "blackops_atitc.dz", "blackops_dxt.dz",
                                     "blackops_gles1.dz"};
    for (size_t i = 0; !g_dtrz.path[0] && i < sizeof(archives) / sizeof(archives[0]); ++i) {
        char candidate[1200];
        snprintf(candidate, sizeof(candidate), "%s/assets/%s", g_root, archives[i]);
        if (path_exists(candidate)) {
            snprintf(g_dtrz.path, sizeof(g_dtrz.path), "%s", candidate);
            break;
        }
    }
    FILE *file = g_dtrz.path[0] ? fopen(g_dtrz.path, "rb") : NULL;
    if (!file) {
        log_error("game archive (blackops_etc.dz) not found in %s/assets", g_root);
        return 0;
    }
    uint8_t header[9];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) || memcmp(header, "DTRZ", 4)) {
        fclose(file);
        return 0;
    }
    uint16_t file_count = le16(header + 4);
    uint16_t group_count = le16(header + 6);
    if (group_count > 0) {
        group_count--;
    }
    if (file_count > DTRZ_MAX_ENTRIES) {
        fclose(file);
        return 0;
    }
    for (uint16_t i = 0; i < file_count; ++i) {
        if (!read_c_string(file, g_dtrz.entries[i].name, DTRZ_NAME_MAX)) {
            fclose(file);
            return 0;
        }
    }
    /* directory names: several files share a name (every map has loading/<map>/loading_droid...),
     * so the directory is needed to tell them apart */
    static char dirs[DTRZ_MAX_ENTRIES][DTRZ_NAME_MAX];
    for (uint16_t i = 0; i < group_count && i < DTRZ_MAX_ENTRIES; ++i) {
        if (!read_c_string(file, dirs[i], DTRZ_NAME_MAX)) {
            fclose(file);
            return 0;
        }
    }
    /* u16 marker (1), then per file {u16 index, u16 0xffff, u16 directory (2 = first name above)}, 2 bytes pad */
    uint8_t marker[2];
    if (fread(marker, 1, 2, file) != 2 || le16(marker) != 1) {
        fclose(file);
        return 0;
    }
    static uint16_t file_dir[DTRZ_MAX_ENTRIES];
    for (uint16_t i = 0; i < file_count; ++i) {
        uint8_t rec[6];
        if (fread(rec, 1, 6, file) != 6) {
            fclose(file);
            return 0;
        }
        file_dir[i] = le16(rec + 4);
    }
    if (fseek(file, 2L, SEEK_CUR) != 0) {
        fclose(file);
        return 0;
    }
    for (uint16_t i = 0; i < file_count; ++i) {
        uint8_t record[16];
        if (fread(record, 1, sizeof(record), file) != sizeof(record)) {
            fclose(file);
            return 0;
        }
        struct dtrz_entry *entry = &g_dtrz.entries[i];
        entry->offset = le32(record);
        entry->size = le32(record + 4);
        char full[DTRZ_NAME_MAX * 2];
        int d = (int)file_dir[i] - 2;
        if (d >= 0 && d < group_count) {
            snprintf(full, sizeof(full), "%s/%s", dirs[d], entry->name);
        } else {
            snprintf(full, sizeof(full), "%s", entry->name);
        }
        for (char *c = full; *c; ++c) {
            if (*c == '\\') *c = '/';
        }
        lower_copy(entry->lower_name, DTRZ_NAME_MAX, full); /* "loading/kino/loading_droid.group.bin" */
        lower_copy(entry->lower_base, DTRZ_NAME_MAX, base_name(entry->name));
    }
    g_dtrz.count = file_count;
    fclose(file);
    log_info("archive %s: %zu entries", g_dtrz.path, g_dtrz.count);
    if (getenv("CODBOZ_LIST_ARCHIVE")) {
        for (size_t i = 0; i < g_dtrz.count; ++i) {
            if (strstr(g_dtrz.entries[i].lower_name, getenv("CODBOZ_LIST_ARCHIVE"))) {
                log_info("entry %s", g_dtrz.entries[i].lower_name);
            }
        }
    }
    return 1;
}

static const struct dtrz_entry *dtrz_find(const char *name) {
    if (!dtrz_load_index()) {
        return NULL;
    }
    char lower[DTRZ_NAME_MAX];
    lower_copy(lower, sizeof(lower), name);
    const char *lower_base = base_name(lower);
    /* the archive's paths have no "data-etc/" texture-set prefix; normalise slashes ("a//b", "a\\b") */
    char path[DTRZ_NAME_MAX];
    const char *in = lower;
    if (strncmp(in, "data-", 5) == 0 && strchr(in, '/')) {
        in = strchr(in, '/') + 1;
    }
    size_t n = 0;
    for (; *in && n + 1 < sizeof(path); ++in) {
        char c = *in == '\\' ? '/' : *in;
        if (c == '/' && (n == 0 || path[n - 1] == '/')) continue;
        path[n++] = c;
    }
    path[n] = 0;
    /* exact path first: many maps share file names (every loading screen is
     * loading/<map>/loading_droid.group.bin), so a name-only match returned the first map's */
    for (size_t i = 0; i < g_dtrz.count; ++i) {
        if (strcmp(g_dtrz.entries[i].lower_name, path) == 0) {
            return &g_dtrz.entries[i];
        }
    }
    for (size_t i = 0; i < g_dtrz.count; ++i) {
        if (strcmp(g_dtrz.entries[i].lower_base, lower_base) == 0) {
            return &g_dtrz.entries[i];
        }
    }
    return NULL;
}

static int dtrz_prefer(const char *name) {
    char lower[256];
    lower_copy(lower, sizeof(lower), name);
    return strncmp(lower, "data-etc/", 9) == 0 || strncmp(lower, "data-dxt/", 9) == 0 ||
           strncmp(lower, "data-atitc/", 11) == 0;
}

static uint32_t open_dtrz(const char *name) {
    const struct dtrz_entry *entry = dtrz_find(name);
    if (!entry) {
        return 0;
    }
    if (!g_dtrz_fp) {
        g_dtrz_fp = fopen(g_dtrz.path, "rb");
    }
    if (!g_dtrz_fp) {
        return 0;
    }
    uint8_t *buffer = malloc(entry->size ? entry->size : 1);
    clearerr(g_dtrz_fp);
    if (!buffer || fseek(g_dtrz_fp, (long)entry->offset, SEEK_SET) != 0 ||
        fread(buffer, 1, entry->size, g_dtrz_fp) != entry->size) {
        free(buffer);
        return 0;
    }
    uint32_t size = entry->size;
    codboz_patch_archive_entry(entry->name, &buffer, &size);
    return vfile_new(NULL, buffer, size, 1);
}

void codboz_pc_texts(const char *name, uint8_t *data, uint32_t size);

static uint32_t open_patched_copy(const char *path, const char *name) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    fseek(file, 0, SEEK_END);
    long len = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *data = malloc(len > 0 ? (size_t)len : 1);
    if (!data || fread(data, 1, (size_t)len, file) != (size_t)len) {
        fclose(file);
        free(data);
        return 0;
    }
    fclose(file);
    codboz_pc_texts(name, data, (uint32_t)len);
    return vfile_new(NULL, data, (uint32_t)len, 1);
}

static int try_asset(char *out, size_t out_size, const char *prefix, const char *name) {
    snprintf(out, out_size, "%s/%s/%s", g_root, prefix, name);
    return path_exists(out);
}

static int flat_group_name(const char *name, char *out, size_t out_size) {
    const char *slash = strchr(name, '/');
    if (!slash || !strstr(name, ".group.bin")) {
        return 0;
    }
    size_t section_len = (size_t)(slash - name);
    if (section_len == 0 || section_len >= 128) {
        return 0;
    }
    const char *leaf = strrchr(name, '/') + 1;
    if (strcmp(leaf, ".group.bin") == 0) {
        snprintf(out, out_size, "%.*s.group.bin", (int)section_len, name);
    } else {
        snprintf(out, out_size, "%s", leaf);
    }
    return 1;
}

/* Resolve a read-only data file; the archive itself may be asked for by its gles1 name. */
static int resolve_read_path(const char *name, char *out, size_t out_size) {
    if (strcasecmp(base_name(name), "blackops_gles1.dz") == 0 && dtrz_load_index()) {
        snprintf(out, out_size, "%s", g_dtrz.path);
        return 1;
    }
    char flat[512];
    const char *leaf = base_name(name);
    if (try_asset(out, out_size, ".", name) || try_asset(out, out_size, "assets", name) ||
        try_asset(out, out_size, "assets/data-gles1", name) ||
        try_asset(out, out_size, "assets/data-sw", name)) {
        return 1;
    }
    if (flat_group_name(name, flat, sizeof(flat)) &&
        (try_asset(out, out_size, "assets/data-gles1", flat) ||
         try_asset(out, out_size, "assets", flat))) {
        return 1;
    }
    if (leaf != name && (try_asset(out, out_size, "assets/data-gles1", leaf) ||
                         try_asset(out, out_size, "assets", leaf))) {
        return 1;
    }
    return 0;
}

static int is_user_name(const char *name) {
    const char *dot = strrchr(name, '.');
    return (dot && (strcasecmp(dot, ".i3d") == 0)) || strcasecmp(base_name(name), "boz_name.txt") == 0;
}

static int is_write_mode(const char *mode) {
    return mode[0] == 'w' || mode[0] == 'a' || strchr(mode, '+') != NULL;
}

static void user_path(char *out, size_t out_size, const char *name) {
    snprintf(out, out_size, "%s/save/%s", g_user_dir, name);
}

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) {
        return 0;
    }
    make_parent_dirs(dst);
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    uint8_t buffer[8192];
    size_t got;
    while ((got = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        fwrite(buffer, 1, got, out);
    }
    fclose(out);
    fclose(in);
    return 1;
}

static uint32_t file_open(const char *raw_name, const char *raw_mode) {
    const char *name = strip_drive(raw_name ? raw_name : "");
    char mode[16];
    size_t n = 0;
    for (const char *p = raw_mode && raw_mode[0] ? raw_mode : "rb"; *p && n + 2 < sizeof(mode);
         ++p) {
        if (*p != 'U' && *p != 't') {
            mode[n++] = *p;
        }
    }
    mode[n] = 0;
    if (!strchr(mode, 'b')) {
        mode[n++] = 'b';
        mode[n] = 0;
    }

    char path[1200];
    if (is_write_mode(mode) || is_user_name(name)) {
        user_path(path, sizeof(path), name);
        make_parent_dirs(path);
        char seed[1200];
        if (mode[0] == 'r' && !path_exists(path) && !is_user_name(name) &&
            resolve_read_path(name, seed, sizeof(seed))) {
            copy_file(seed, path);
        }
        FILE *file = fopen(path, mode);
        log_debug("open %s (%s) -> %s%s", name, mode, path, file ? "" : " FAILED");
        g_file_error = file ? 0 : 4;
        return file ? vfile_new(file, NULL, 0, 0) : 0;
    }

    uint32_t handle = 0;
    user_path(path, sizeof(path), name);
    if (path_exists(path)) {
        /* something the game wrote earlier shadows the shipped data */
        FILE *file = fopen(path, mode);
        handle = file ? vfile_new(file, NULL, 0, 0) : 0;
    }
    if (!handle && dtrz_prefer(name)) {
        handle = open_dtrz(name);
    }
    if (!handle && resolve_read_path(name, path, sizeof(path))) {
        if (strcasecmp(base_name(name), "blackops_loader.dz") == 0) {
            /* the language tables: read into memory to swap touch/TV prompts for PC ones */
            handle = open_patched_copy(path, name);
        } else {
            FILE *file = fopen(path, mode);
            handle = file ? vfile_new(file, NULL, 0, 0) : 0;
        }
    }
    if (!handle) {
        handle = open_dtrz(name);
    }
    if (!handle && strcmp(base_name(name), "console.bin") == 0) {
        handle = vfile_new(NULL, malloc(1), 0, 1);
    }
    log_debug("open %s (%s)%s", name, mode, handle ? "" : " NOT FOUND");
    g_file_error = handle ? 0 : 4; /* S3E_FILE_ERR_NOT_FOUND */
    return handle;
}

static int32_t vfile_close(struct vfile *f) {
    int32_t rc = 0;
    if (f->file) {
        rc = fclose(f->file);
    }
    if (f->owns_mem) {
        free(f->mem);
    }
    memset(f, 0, sizeof(*f));
    return rc;
}

static uint32_t vfile_read(struct vfile *f, void *buffer, uint32_t size, uint32_t count) {
    if (!size || !count) {
        return 0;
    }
    if (f->file) {
        return (uint32_t)fread(buffer, size, count, f->file);
    }
    uint32_t avail = f->mem_size - f->mem_pos;
    uint32_t items = avail / size < count ? avail / size : count;
    memcpy(buffer, f->mem + f->mem_pos, (size_t)items * size);
    f->mem_pos += items * size;
    if (items < count) {
        f->eof = 1;
    }
    return items;
}

static int32_t vfile_size(struct vfile *f) {
    if (!f->file) {
        return (int32_t)f->mem_size;
    }
    long here = ftell(f->file);
    fseek(f->file, 0, SEEK_END);
    long size = ftell(f->file);
    fseek(f->file, here, SEEK_SET);
    return (int32_t)size;
}

IMPORT(s3eFileOpen) {
    ret(file_open(G2H(arg(0)), G2H(arg(1))));
}

IMPORT(s3eFileOpenFromMemory) {
    ret(arg(0) && arg(1) ? vfile_new(NULL, G2H(arg(0)), arg(1), 0) : 0);
}

IMPORT(s3eFileClose) {
    struct vfile *f = vfile_of(arg(0));
    ret(f ? (uint32_t)vfile_close(f) : 1);
}

IMPORT(s3eFileRead) {
    struct vfile *f = vfile_of(arg(3));
    ret(f ? vfile_read(f, G2H(arg(0)), arg(1), arg(2)) : 0);
}

IMPORT(s3eFileWrite) {
    struct vfile *f = vfile_of(arg(3));
    ret(f && f->file ? (uint32_t)fwrite(G2H(arg(0)), arg(1), arg(2), f->file) : 0);
}

IMPORT(s3eFileGetChar) {
    struct vfile *f = vfile_of(arg(0));
    uint8_t c;
    ret(f && vfile_read(f, &c, 1, 1) == 1 ? c : (uint32_t)-1);
}

IMPORT(s3eFilePutChar) {
    struct vfile *f = vfile_of(arg(1));
    ret(f && f->file ? (uint32_t)fputc((int)arg(0), f->file) : (uint32_t)-1);
}

IMPORT(s3eFileFlush) {
    struct vfile *f = vfile_of(arg(0));
    ret(f && f->file ? (uint32_t)fflush(f->file) : 0);
}

IMPORT(s3eFileSeek) {
    struct vfile *f = vfile_of(arg(0));
    int32_t offset = (int32_t)arg(1);
    int origin = (int)arg(2); /* S3E_FILESEEK_SET/CUR/END = 0/1/2 */
    if (!f) {
        ret(1);
        return;
    }
    if (f->file) {
        ret(fseek(f->file, offset, origin == 1 ? SEEK_CUR : origin == 2 ? SEEK_END : SEEK_SET)
                ? 1
                : 0);
        return;
    }
    int64_t base = origin == 1 ? f->mem_pos : origin == 2 ? f->mem_size : 0;
    int64_t pos = base + offset;
    if (pos < 0 || pos > f->mem_size) {
        ret(1);
        return;
    }
    f->mem_pos = (uint32_t)pos;
    f->eof = 0;
    ret(0);
}

IMPORT(s3eFileTell) {
    struct vfile *f = vfile_of(arg(0));
    ret(!f ? (uint32_t)-1 : f->file ? (uint32_t)ftell(f->file) : f->mem_pos);
}

IMPORT(s3eFileGetSize) {
    struct vfile *f = vfile_of(arg(0));
    ret(f ? (uint32_t)vfile_size(f) : (uint32_t)-1);
}

IMPORT(s3eFileGetFileInt) {
    ret(0);
}

IMPORT(s3eFileCheckExists) {
    const char *name = strip_drive(G2H(arg(0)) ? G2H(arg(0)) : "");
    char path[1200];
    if (is_user_name(name)) {
        user_path(path, sizeof(path), name);
        ret(path_exists(path));
        return;
    }
    user_path(path, sizeof(path), name);
    ret(path_exists(path) || resolve_read_path(name, path, sizeof(path)) || dtrz_find(name) ? 1
                                                                                           : 0);
}

IMPORT(s3eFileGetError) {
    ret((uint32_t)g_file_error);
}

IMPORT(s3eFileGetErrorString) {
    static uint32_t text;
    if (!text) {
        text = arena_strdup("file error");
    }
    ret(text);
}

IMPORT(s3eFileMakeDirectory) {
    char path[1200];
    user_path(path, sizeof(path), strip_drive(G2H(arg(0))));
    make_parent_dirs(path);
    ret(mkdir_one(path) == 0 || errno == EEXIST ? 0 : 1);
}

IMPORT(s3eFileDelete) {
    char path[1200];
    user_path(path, sizeof(path), strip_drive(G2H(arg(0))));
    ret(remove(path) == 0 ? 0 : 1);
}

IMPORT(s3eFileRename) {
    char from[1200], to[1200];
    user_path(from, sizeof(from), strip_drive(G2H(arg(0))));
    user_path(to, sizeof(to), strip_drive(G2H(arg(1))));
    remove(to);
    ret(rename(from, to) == 0 ? 0 : 1);
}

IMPORT(s3eFileAddUserFileSys) {
    ret(0);
}

IMPORT(s3eFileListDirectory) {
    ret(0);
}

IMPORT(s3eFileListClose) {
    ret(0);
}

void register_file_imports(void) {
    REG(s3eFileOpen);
    REG(s3eFileOpenFromMemory);
    REG(s3eFileClose);
    REG(s3eFileRead);
    REG(s3eFileWrite);
    REG(s3eFileGetChar);
    REG(s3eFilePutChar);
    REG(s3eFileFlush);
    REG(s3eFileSeek);
    REG(s3eFileTell);
    REG(s3eFileGetSize);
    REG(s3eFileGetFileInt);
    REG(s3eFileCheckExists);
    REG(s3eFileGetError);
    REG(s3eFileGetErrorString);
    REG(s3eFileMakeDirectory);
    REG(s3eFileDelete);
    REG(s3eFileRename);
    REG(s3eFileAddUserFileSys);
    REG(s3eFileListDirectory);
    REG(s3eFileListClose);
}
