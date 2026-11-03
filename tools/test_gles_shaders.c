/*
 * Regression test of the Android shader path on a real OpenGL ES driver (Mesa surfaceless), with
 * no game data. The game sends one string per line (165 and 182 strings) with nested #if blocks;
 * a cap on the string count cut them inside a block ("Unterminated #if"), no program linked, and
 * every glUseProgram raised GL_INVALID_OPERATION.
 *
 *   cc -Isrc tools/test_gles_shaders.c src/gles_shader.c -ldl -o test_gles_shaders
 *   ./test_gles_shaders [folder of .glsl files from <data>/shader-dump]
 *
 * Exit status 0 = every check passed. Exit status 77 = no OpenGL ES 3 context here (skipped).
 */
#include "gles_shader.h"

#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef void *ptr;
static ptr (*eglGetPlatformDisplay_)(unsigned, void *, const intptr_t *);
static unsigned (*eglInitialize_)(ptr, int *, int *);
static unsigned (*eglBindAPI_)(unsigned);
static unsigned (*eglChooseConfig_)(ptr, const int *, ptr *, int, int *);
static ptr (*eglCreateContext_)(ptr, ptr, ptr, const int *);
static unsigned (*eglMakeCurrent_)(ptr, ptr, ptr, ptr);
static void *(*eglGetProcAddress_)(const char *);

static unsigned (*glCreateShader)(unsigned);
static void (*glShaderSource)(unsigned, int, const char *const *, const int *);
static void (*glCompileShader)(unsigned);
static void (*glGetShaderiv)(unsigned, unsigned, int *);
static void (*glGetShaderInfoLog)(unsigned, int, int *, char *);
static unsigned (*glCreateProgram)(void);
static void (*glAttachShader)(unsigned, unsigned);
static void (*glLinkProgram)(unsigned);
static void (*glGetProgramiv)(unsigned, unsigned, int *);
static void (*glUseProgram)(unsigned);
static unsigned (*glGetError)(void);
static const unsigned char *(*glGetString)(unsigned);
static int (*glGetAttribLocation)(unsigned, const char *);
static void (*glVertexAttribPointer)(unsigned, int, unsigned, unsigned char, int, const void *);
static void (*glEnableVertexAttribArray)(unsigned);
static void (*glDrawArrays)(unsigned, int, int);
static void (*glReadPixels)(int, int, int, int, unsigned, unsigned, void *);
static void (*glGenFramebuffers)(int, unsigned *);
static void (*glBindFramebuffer)(unsigned, unsigned);
static void (*glGenRenderbuffers)(int, unsigned *);
static void (*glBindRenderbuffer)(unsigned, unsigned);
static void (*glRenderbufferStorage)(unsigned, unsigned, int, int);
static void (*glFramebufferRenderbuffer)(unsigned, unsigned, unsigned, unsigned);
static void (*glViewport)(int, int, int, int);

enum { VERTEX = 0x8b31, FRAGMENT = 0x8b30 };
static int g_failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            printf("ok   ");                                                                       \
        } else {                                                                                   \
            printf("FAIL ");                                                                       \
            g_failures++;                                                                          \
        }                                                                                          \
        printf(__VA_ARGS__);                                                                       \
        putchar('\n');                                                                             \
    } while (0)

static int load_gl(void) {
    void *egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!egl || !dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL)) {
        return 0;
    }
    *(void **)&eglGetPlatformDisplay_ = dlsym(egl, "eglGetPlatformDisplay");
    *(void **)&eglInitialize_ = dlsym(egl, "eglInitialize");
    *(void **)&eglBindAPI_ = dlsym(egl, "eglBindAPI");
    *(void **)&eglChooseConfig_ = dlsym(egl, "eglChooseConfig");
    *(void **)&eglCreateContext_ = dlsym(egl, "eglCreateContext");
    *(void **)&eglMakeCurrent_ = dlsym(egl, "eglMakeCurrent");
    *(void **)&eglGetProcAddress_ = dlsym(egl, "eglGetProcAddress");
    if (!eglGetPlatformDisplay_ || !eglGetProcAddress_) {
        return 0;
    }
    ptr display = eglGetPlatformDisplay_(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, NULL, NULL);
    int major, minor, count = 0;
    ptr config = NULL;
    static const int config_attribs[] = {0x3040 /* RENDERABLE_TYPE */, 0x40 /* ES3 */, 0x3033 /* SURFACE_TYPE */, 0, 0x3038};
    static const int context_attribs[] = {0x3098, 3, 0x3038};
    if (!display || !eglInitialize_(display, &major, &minor) || !eglBindAPI_(0x30a0) ||
        !eglChooseConfig_(display, config_attribs, &config, 1, &count) || count < 1) {
        return 0;
    }
    ptr context = eglCreateContext_(display, config, NULL, context_attribs);
    if (!context || !eglMakeCurrent_(display, NULL, NULL, context)) {
        return 0;
    }
#define L(name) *(void **)&name = eglGetProcAddress_(#name)
    L(glCreateShader); L(glShaderSource); L(glCompileShader); L(glGetShaderiv); L(glGetShaderInfoLog);
    L(glCreateProgram); L(glAttachShader); L(glLinkProgram); L(glGetProgramiv); L(glUseProgram);
    L(glGetError); L(glGetString); L(glGetAttribLocation); L(glVertexAttribPointer);
    L(glEnableVertexAttribArray); L(glDrawArrays); L(glReadPixels); L(glGenFramebuffers);
    L(glBindFramebuffer); L(glGenRenderbuffers); L(glBindRenderbuffer); L(glRenderbufferStorage);
    L(glFramebufferRenderbuffer); L(glViewport);
#undef L
    return 1;
}

static unsigned compile_text(unsigned type, const char *text, char *log, int log_size) {
    unsigned shader = glCreateShader(type);
    int ok = 0;
    glShaderSource(shader, 1, &text, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, 0x8b81, &ok);
    log[0] = 0;
    if (!ok) {
        glGetShaderInfoLog(shader, log_size, NULL, log);
        return 0;
    }
    return shader;
}

/* The same shape as the game's: a comment first, one string per line, #if blocks that close far
 * past the 64th string. Own text, no game data. */
static int make_shader(int vertex, int want, char ***out) {
    char **lines = calloc((size_t)want + 8, sizeof(*lines));
    int n = 0;
#define ADD(...) do { char b[128]; snprintf(b, sizeof(b), __VA_ARGS__); lines[n++] = strdup(b); } while (0)
    ADD("//Test%sShader\n", vertex ? "Vertex" : "Fragment");
    ADD("#define STREAM_A \n");
    ADD("#if defined STREAM_A\n");
    if (!vertex) {
        ADD("precision mediump float;\n");
    }
    ADD("#define UNUSED_FIRST 1\n");
    int fill = want - n - (vertex ? 7 : 5);
    for (int i = 0; i < fill; ++i) {
        ADD("#define UNUSED_%d %d\n", i, i);
    }
    ADD("#endif\n");
    if (vertex) {
        ADD("attribute highp vec4 inVert;\n");
        ADD("varying mediump vec4 outCol;\n");
        ADD("void main(void)\n{\n");
        ADD("  gl_Position = inVert;\n");
        ADD("  outCol = vec4(0.0, 1.0, 0.0, 1.0);\n");
        ADD("}\n");
    } else {
        ADD("varying mediump vec4 outCol;\n");
        ADD("void main(void)\n{\n");
        ADD("  gl_FragColor = outCol;\n");
        ADD("}\n");
    }
#undef ADD
    *out = lines;
    return n;
}

static void test_join(void) {
    /* lengths honoured: fragments need no terminator and may hold more bytes than the length */
    const char *strings[] = {"abcXXX", "de", "fXX"};
    const int32_t lengths[] = {3, -1, 1};
    size_t size = 0;
    char *text = gles_shader_join(3, strings, lengths, &size);
    CHECK(text && size == 6 && strcmp(text, "abcdef") == 0, "join honours lengths and -1 (\"%s\")", text ? text : "");
    free(text);
    text = gles_shader_join(3, strings, NULL, &size);
    CHECK(text && strcmp(text, "abcXXXdefXX") == 0, "join with NULL lengths reads to the terminator");
    free(text);
    const char *hole[] = {"a", NULL};
    CHECK(gles_shader_join(2, hole, NULL, NULL) == NULL, "a NULL fragment is refused");
    /* far more strings than any fixed table */
    enum { MANY = 100000 };
    const char **many = malloc(MANY * sizeof(*many));
    for (int i = 0; i < MANY; ++i) {
        many[i] = i == MANY - 1 ? "Z" : "a";
    }
    text = gles_shader_join(MANY, many, NULL, &size);
    CHECK(text && size == MANY && text[MANY - 1] == 'Z', "%d fragments keep the last one", MANY);
    free(text);
    free(many);
}

static void test_dump(void) {
    char dir[] = "/tmp/boz-shader-test-XXXXXX", path[256], back[64] = "";
    if (!mkdtemp(dir)) {
        CHECK(0, "temporary folder");
        return;
    }
    static const char text[] = "line one\nline two\n";
    size_t size = sizeof(text) - 1;
    uint64_t hash = gles_shader_hash(text, size);
    CHECK(gles_shader_dump(dir, hash, text, size, path, sizeof(path)), "dump writes the file");
    FILE *f = fopen(path, "rb");
    size_t got = f ? fread(back, 1, sizeof(back), f) : 0;
    if (f) fclose(f);
    CHECK(got == size && memcmp(back, text, size) == 0, "dump holds the exact text");
    CHECK(gles_shader_hash(text, size) != gles_shader_hash(text, size - 1), "hash sees a cut text");
    remove(path);
    rmdir(dir);
}

static unsigned build_pair(int count_v, char **v, int count_f, char **f, char *log, int log_size) {
    char *tv = gles_shader_join(count_v, (const char *const *)v, NULL, NULL);
    char *tf = gles_shader_join(count_f, (const char *const *)f, NULL, NULL);
    unsigned sv = compile_text(VERTEX, tv, log, log_size);
    unsigned sf = sv ? compile_text(FRAGMENT, tf, log, log_size) : 0;
    free(tv);
    free(tf);
    if (!sv || !sf) {
        return 0;
    }
    unsigned program = glCreateProgram();
    int ok = 0;
    glAttachShader(program, sv);
    glAttachShader(program, sf);
    glLinkProgram(program);
    glGetProgramiv(program, 0x8b82, &ok);
    return ok ? program : 0;
}

static void test_driver(void) {
    char log[2048];
    char **v, **f;
    int nv = make_shader(1, 165, &v), nf = make_shader(0, 182, &f);
    CHECK(nv == 165 && nf == 182, "test shaders have the game's string counts (%d, %d)", nv, nf);

    /* the old bug, proven on this driver: 64 strings end inside the #if */
    char *cut = gles_shader_join(64, (const char *const *)v, NULL, NULL);
    CHECK(compile_text(VERTEX, cut, log, sizeof(log)) == 0, "cut at 64 strings fails to compile, as on the phone");
    free(cut);
    printf("     driver said: %s", log);

    while (glGetError()) {}
    unsigned program = build_pair(nv, v, nf, f, log, sizeof(log));
    CHECK(program != 0, "all strings compile and link (%s)", program ? "linked" : log);
    if (!program) {
        return;
    }
    glUseProgram(program);
    CHECK(glGetError() == 0, "glUseProgram of the linked program raises no error");

    /* it really runs: a green triangle over the whole target */
    unsigned fbo, rb;
    static const float tri[] = {-1, -1, 3, -1, -1, 3};
    unsigned char pixel[4] = {0};
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(0x8d40, fbo);
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(0x8d41, rb);
    glRenderbufferStorage(0x8d41, 0x8058 /* RGBA8 */, 4, 4);
    glFramebufferRenderbuffer(0x8d40, 0x8ce0, 0x8d41, rb);
    glViewport(0, 0, 4, 4);
    int attrib = glGetAttribLocation(program, "inVert");
    CHECK(attrib >= 0, "attribute found");
    glEnableVertexAttribArray((unsigned)attrib);
    glVertexAttribPointer((unsigned)attrib, 2, 0x1406, 0, 0, tri);
    glDrawArrays(4, 0, 3);
    glReadPixels(1, 1, 1, 1, 0x1908, 0x1401, pixel);
    CHECK(pixel[1] == 255 && pixel[0] == 0, "draw produces the shader's colour (%d,%d,%d)", pixel[0], pixel[1], pixel[2]);
    CHECK(glGetError() == 0, "no GL error after drawing");

    /* the failure mode behind the phone's 0x0502: a program that did not link */
    unsigned broken = glCreateProgram();
    glLinkProgram(broken);
    glUseProgram(broken);
    CHECK(glGetError() == 0x0502, "glUseProgram of an unlinked program is GL_INVALID_OPERATION");
}

/* Optional: every file of a shader dump (<data>/shader-dump) must compile on this driver. */
static void test_dump_folder(const char *folder) {
    DIR *dir = opendir(folder);
    if (!dir) {
        CHECK(0, "cannot open %s", folder);
        return;
    }
    int total = 0, failed = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        char path[2048], log[2048];
        size_t len = strlen(entry->d_name);
        if (len < 5 || strcmp(entry->d_name + len - 5, ".glsl") != 0) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) {
            continue;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);
        char *text = malloc((size_t)size + 1);
        size_t got = fread(text, 1, (size_t)size, f);
        text[got] = 0;
        fclose(f);
        unsigned type = strstr(text, "gl_Position") ? VERTEX : FRAGMENT;
        total++;
        if (!compile_text(type, text, log, sizeof(log))) {
            failed++;
            printf("FAIL %s: %s", entry->d_name, log);
        }
        free(text);
    }
    closedir(dir);
    CHECK(total > 0 && failed == 0, "shader dump: %d files, %d failed", total, failed);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    test_join();
    test_dump();
    if (!load_gl()) {
        printf("SKIP: no OpenGL ES 3 surfaceless context (driver tests not run)\n");
        return g_failures ? 1 : 77;
    }
    printf("driver: %s / %s\n", glGetString(0x1f02), glGetString(0x1f01));
    test_driver();
    if (argc > 1) {
        test_dump_folder(argv[1]);
    }
    printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
