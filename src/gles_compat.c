/*
 * OpenGL ES 3 stand-ins for the desktop GL calls the runtime's own drawing uses (ui.c: immediate
 * mode quads/fans/triangles, matrix stacks, Push/PopAttrib). Built only with CODBOZ_GLES (Android);
 * gl_proc() asks here first, so ui.c keeps one code path for both platforms.
 *
 * The game itself is not affected: it already speaks OpenGL ES 2.0, which an ES 3 context runs
 * as is. The only shared entry points are glEnable/glDisable, which drop the fixed-function caps
 * (TEXTURE_2D, ALPHA_TEST, LIGHTING, FOG) ES rejects; an ES 2 game never sets those.
 */
#ifdef CODBOZ_GLES
#include "host.h"

#include <SDL.h>
#include <math.h>

enum {
    GL_TEXTURE_2D_ = 0x0de1,
    GL_ALPHA_TEST_ = 0x0bc0,
    GL_LIGHTING_ = 0x0b50,
    GL_FOG_ = 0x0b60,
    GL_TRIANGLES_ = 0x0004,
    GL_TRIANGLE_FAN_ = 0x0006,
    GL_QUADS_ = 0x0007,
    GL_MODELVIEW_ = 0x1700,
    GL_PROJECTION_ = 0x1701,
    GL_FLOAT_ = 0x1406,
    STACK_DEPTH = 8,
    MAX_VERTICES = 4096,
    ATTRIBS = 3, /* position, texture coordinate, colour */
};

typedef float mat4[16]; /* column-major, as GL */

struct vertex {
    float x, y, u, v, r, g, b, a;
};

static struct {
    void (*Enable)(uint32_t);
    void (*Disable)(uint32_t);
    uint8_t (*IsEnabled)(uint32_t);
    void (*GetIntegerv)(uint32_t, int32_t *);
    void (*GetBooleanv)(uint32_t, uint8_t *);
    void (*BlendFuncSeparate)(uint32_t, uint32_t, uint32_t, uint32_t);
    void (*BlendEquationSeparate)(uint32_t, uint32_t);
    void (*Viewport)(int32_t, int32_t, int32_t, int32_t);
    void (*Scissor)(int32_t, int32_t, int32_t, int32_t);
    void (*ColorMask)(uint8_t, uint8_t, uint8_t, uint8_t);
    void (*DepthMask)(uint8_t);
    void (*StencilFunc)(uint32_t, int32_t, uint32_t);
    void (*StencilOp)(uint32_t, uint32_t, uint32_t);
    void (*StencilMask)(uint32_t);
    void (*CullFace)(uint32_t);
    void (*GetVertexAttribiv)(uint32_t, uint32_t, int32_t *);
    void (*GetVertexAttribPointerv)(uint32_t, uint32_t, void **);
    void (*VertexAttribPointer)(uint32_t, int32_t, uint32_t, uint8_t, int32_t, const void *);
    void (*EnableVertexAttribArray)(uint32_t);
    void (*DisableVertexAttribArray)(uint32_t);
    void (*BindBuffer)(uint32_t, uint32_t);
    void (*DrawArrays)(uint32_t, int32_t, int32_t);
    uint32_t (*CreateShader)(uint32_t);
    void (*ShaderSource)(uint32_t, int32_t, const char *const *, const int32_t *);
    void (*CompileShader)(uint32_t);
    void (*GetShaderiv)(uint32_t, uint32_t, int32_t *);
    void (*GetShaderInfoLog)(uint32_t, int32_t, int32_t *, char *);
    uint32_t (*CreateProgram)(void);
    void (*AttachShader)(uint32_t, uint32_t);
    void (*BindAttribLocation)(uint32_t, uint32_t, const char *);
    void (*LinkProgram)(uint32_t);
    void (*GetProgramiv)(uint32_t, uint32_t, int32_t *);
    void (*UseProgram)(uint32_t);
    int32_t (*GetUniformLocation)(uint32_t, const char *);
    void (*UniformMatrix4fv)(int32_t, int32_t, uint8_t, const float *);
    void (*Uniform1i)(int32_t, int32_t);
    uint32_t (*GetError)(void);
} es;

static bool g_loaded;
static bool g_textured; /* glEnable(GL_TEXTURE_2D) as the fixed pipeline would see it */
static uint32_t g_program;
static int32_t g_u_mvp = -1, g_u_textured = -1, g_u_tex = -1;

static mat4 g_stack[2][STACK_DEPTH];
static int g_top[2];
static int g_mode; /* 0 modelview, 1 projection; other matrix modes are accepted and ignored */
static bool g_mode_ignored;

static struct vertex g_vertices[MAX_VERTICES];
static int g_count;
static uint32_t g_prim;
static float g_color[4] = {1, 1, 1, 1}, g_uv[2];

static void *real(const char *name) {
    void *fn = SDL_GL_GetProcAddress(name);
    if (!fn) {
        log_error("GLES: %s is missing", name);
    }
    return fn;
}

#define LOAD(field, name) *(void **)&es.field = real(name)

static void load(void) {
    if (g_loaded) {
        return;
    }
    g_loaded = true;
    LOAD(Enable, "glEnable");
    LOAD(Disable, "glDisable");
    LOAD(IsEnabled, "glIsEnabled");
    LOAD(GetIntegerv, "glGetIntegerv");
    LOAD(GetBooleanv, "glGetBooleanv");
    LOAD(BlendFuncSeparate, "glBlendFuncSeparate");
    LOAD(BlendEquationSeparate, "glBlendEquationSeparate");
    LOAD(Viewport, "glViewport");
    LOAD(Scissor, "glScissor");
    LOAD(ColorMask, "glColorMask");
    LOAD(DepthMask, "glDepthMask");
    LOAD(StencilFunc, "glStencilFunc");
    LOAD(StencilOp, "glStencilOp");
    LOAD(StencilMask, "glStencilMask");
    LOAD(CullFace, "glCullFace");
    LOAD(GetVertexAttribiv, "glGetVertexAttribiv");
    LOAD(GetVertexAttribPointerv, "glGetVertexAttribPointerv");
    LOAD(VertexAttribPointer, "glVertexAttribPointer");
    LOAD(EnableVertexAttribArray, "glEnableVertexAttribArray");
    LOAD(DisableVertexAttribArray, "glDisableVertexAttribArray");
    LOAD(BindBuffer, "glBindBuffer");
    LOAD(DrawArrays, "glDrawArrays");
    LOAD(CreateShader, "glCreateShader");
    LOAD(ShaderSource, "glShaderSource");
    LOAD(CompileShader, "glCompileShader");
    LOAD(GetShaderiv, "glGetShaderiv");
    LOAD(GetShaderInfoLog, "glGetShaderInfoLog");
    LOAD(CreateProgram, "glCreateProgram");
    LOAD(AttachShader, "glAttachShader");
    LOAD(BindAttribLocation, "glBindAttribLocation");
    LOAD(LinkProgram, "glLinkProgram");
    LOAD(GetProgramiv, "glGetProgramiv");
    LOAD(UseProgram, "glUseProgram");
    LOAD(GetUniformLocation, "glGetUniformLocation");
    LOAD(UniformMatrix4fv, "glUniformMatrix4fv");
    LOAD(Uniform1i, "glUniform1i");
    LOAD(GetError, "glGetError");
    for (int m = 0; m < 2; ++m) {
        for (int i = 0; i < 16; ++i) {
            g_stack[m][0][i] = i % 5 == 0 ? 1.0f : 0.0f;
        }
    }
}

/* ---- program ---- */

static const char k_vs[] =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "attribute vec4 a_color;\n"
    "uniform mat4 u_mvp;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    v_color = a_color;\n"
    "    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char k_fs[] =
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "uniform int u_textured;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "    gl_FragColor = u_textured != 0 ? v_color * texture2D(u_tex, v_uv) : v_color;\n"
    "}\n";

static uint32_t compile(uint32_t type, const char *source) {
    uint32_t shader = es.CreateShader(type);
    es.ShaderSource(shader, 1, &source, NULL);
    es.CompileShader(shader);
    int32_t ok = 0;
    es.GetShaderiv(shader, 0x8b81, &ok); /* COMPILE_STATUS */
    if (!ok) {
        char text[1024] = "";
        es.GetShaderInfoLog(shader, sizeof(text), NULL, text);
        log_error("GLES compat shader: %s", text);
    }
    return shader;
}

static bool program(void) {
    if (g_program) {
        return true;
    }
    uint32_t vs = compile(0x8b31, k_vs), fs = compile(0x8b30, k_fs);
    uint32_t p = es.CreateProgram();
    es.AttachShader(p, vs);
    es.AttachShader(p, fs);
    es.BindAttribLocation(p, 0, "a_pos");
    es.BindAttribLocation(p, 1, "a_uv");
    es.BindAttribLocation(p, 2, "a_color");
    es.LinkProgram(p);
    int32_t ok = 0;
    es.GetProgramiv(p, 0x8b82, &ok); /* LINK_STATUS */
    if (!ok) {
        log_error("GLES compat program failed to link");
        return false;
    }
    g_program = p;
    g_u_mvp = es.GetUniformLocation(p, "u_mvp");
    g_u_textured = es.GetUniformLocation(p, "u_textured");
    g_u_tex = es.GetUniformLocation(p, "u_tex");
    return true;
}

/* ---- matrices ---- */

static float *top(void) {
    return g_stack[g_mode][g_top[g_mode]];
}

static void multiply(const float *m) {
    float *t = top(), r[16];
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) {
                s += t[k * 4 + row] * m[c * 4 + k];
            }
            r[c * 4 + row] = s;
        }
    }
    memcpy(t, r, sizeof(r));
}

static void c_glMatrixMode(uint32_t mode) {
    load();
    g_mode_ignored = mode != GL_MODELVIEW_ && mode != GL_PROJECTION_;
    if (!g_mode_ignored) {
        g_mode = mode == GL_PROJECTION_;
    }
}

static void c_glPushMatrix(void) {
    if (g_mode_ignored || g_top[g_mode] + 1 >= STACK_DEPTH) {
        return;
    }
    memcpy(g_stack[g_mode][g_top[g_mode] + 1], top(), sizeof(mat4));
    ++g_top[g_mode];
}

static void c_glPopMatrix(void) {
    if (!g_mode_ignored && g_top[g_mode] > 0) {
        --g_top[g_mode];
    }
}

static void c_glLoadIdentity(void) {
    if (g_mode_ignored) {
        return;
    }
    float *t = top();
    for (int i = 0; i < 16; ++i) {
        t[i] = i % 5 == 0 ? 1.0f : 0.0f;
    }
}

static void c_glOrtho(double l, double r, double b, double t, double n, double f) {
    if (g_mode_ignored) {
        return;
    }
    float m[16] = {0};
    m[0] = (float)(2.0 / (r - l));
    m[5] = (float)(2.0 / (t - b));
    m[10] = (float)(-2.0 / (f - n));
    m[12] = (float)(-(r + l) / (r - l));
    m[13] = (float)(-(t + b) / (t - b));
    m[14] = (float)(-(f + n) / (f - n));
    m[15] = 1.0f;
    multiply(m);
}

static void c_glTranslatef(float x, float y, float z) {
    if (g_mode_ignored) {
        return;
    }
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1};
    multiply(m);
}

static void c_glRotatef(float degrees, float x, float y, float z) {
    if (g_mode_ignored) {
        return;
    }
    float len = sqrtf(x * x + y * y + z * z);
    if (len <= 0.0f) {
        return;
    }
    x /= len;
    y /= len;
    z /= len;
    float a = degrees * 3.14159265358979f / 180.0f, c = cosf(a), s = sinf(a), k = 1.0f - c;
    float m[16] = {x * x * k + c,     y * x * k + z * s, x * z * k - y * s, 0,
                   x * y * k - z * s, y * y * k + c,     y * z * k + x * s, 0,
                   x * z * k + y * s, y * z * k - x * s, z * z * k + c,     0,
                   0,                 0,                 0,                 1};
    multiply(m);
}

/* ---- immediate mode ---- */

static void c_glColor4f(float r, float g, float b, float a) {
    g_color[0] = r;
    g_color[1] = g;
    g_color[2] = b;
    g_color[3] = a;
}

static void c_glTexCoord2f(float u, float v) {
    g_uv[0] = u;
    g_uv[1] = v;
}

static void c_glBegin(uint32_t mode) {
    load();
    g_prim = mode;
    g_count = 0;
}

static void c_glVertex2f(float x, float y) {
    if (g_count < MAX_VERTICES) {
        g_vertices[g_count++] =
            (struct vertex){x, y, g_uv[0], g_uv[1], g_color[0], g_color[1], g_color[2], g_color[3]};
    }
}

static struct vertex g_tris[MAX_VERTICES * 2];

static void c_glEnd(void) {
    int n = 0;
    if (g_prim == GL_QUADS_) {
        for (int i = 0; i + 3 < g_count; i += 4) {
            static const int order[6] = {0, 1, 2, 0, 2, 3};
            for (int k = 0; k < 6; ++k) {
                g_tris[n++] = g_vertices[i + order[k]];
            }
        }
    } else if (g_prim == GL_TRIANGLE_FAN_) {
        for (int i = 1; i + 1 < g_count; ++i) {
            g_tris[n++] = g_vertices[0];
            g_tris[n++] = g_vertices[i];
            g_tris[n++] = g_vertices[i + 1];
        }
    } else if (g_prim == GL_TRIANGLES_) {
        n = g_count - g_count % 3;
        memcpy(g_tris, g_vertices, sizeof(struct vertex) * (size_t)n);
    } else {
        log_debug("GLES compat: primitive 0x%x not supported", g_prim);
    }
    g_count = 0;
    if (!n || !program()) {
        return;
    }
    float mvp[16];
    {
        const float *p = g_stack[1][g_top[1]], *m = g_stack[0][g_top[0]];
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                float s = 0;
                for (int k = 0; k < 4; ++k) {
                    s += p[k * 4 + row] * m[c * 4 + k];
                }
                mvp[c * 4 + row] = s;
            }
        }
    }
    es.UseProgram(g_program);
    es.UniformMatrix4fv(g_u_mvp, 1, 0, mvp);
    es.Uniform1i(g_u_textured, g_textured ? 1 : 0);
    es.Uniform1i(g_u_tex, 0);
    es.BindBuffer(0x8892, 0); /* client arrays (ui.c binds VAO 0) */
    const struct vertex *v = g_tris;
    es.VertexAttribPointer(0, 2, GL_FLOAT_, 0, sizeof(*v), &v->x);
    es.VertexAttribPointer(1, 2, GL_FLOAT_, 0, sizeof(*v), &v->u);
    es.VertexAttribPointer(2, 4, GL_FLOAT_, 0, sizeof(*v), &v->r);
    for (uint32_t i = 0; i < ATTRIBS; ++i) {
        es.EnableVertexAttribArray(i);
    }
    es.DrawArrays(GL_TRIANGLES_, 0, n);
    es.UseProgram(0);
}

/* ---- enables ---- */

static bool fixed_function_cap(uint32_t cap) {
    return cap == GL_TEXTURE_2D_ || cap == GL_ALPHA_TEST_ || cap == GL_LIGHTING_ || cap == GL_FOG_;
}

static void c_glEnable(uint32_t cap) {
    load();
    if (cap == GL_TEXTURE_2D_) {
        g_textured = true;
    }
    if (!fixed_function_cap(cap)) {
        es.Enable(cap);
    }
}

static void c_glDisable(uint32_t cap) {
    load();
    if (cap == GL_TEXTURE_2D_) {
        g_textured = false;
    }
    if (!fixed_function_cap(cap)) {
        es.Disable(cap);
    }
}

/* ---- attribute stacks: everything ui.c changes ---- */

static const uint32_t k_caps[] = {0x0be2, 0x0b71, 0x0c11, 0x0b44, 0x0b90, 0x0bd0, 0x8037, 0x809e};
#define CAP_COUNT (sizeof(k_caps) / sizeof(k_caps[0]))

struct saved_server {
    uint8_t caps[CAP_COUNT];
    bool textured;
    int32_t blend[4], equation[2], viewport[4], scissor[4], cull;
    uint8_t color_mask[4], depth_mask;
    int32_t stencil_func, stencil_ref, stencil_value_mask, stencil_fail, stencil_zfail, stencil_zpass,
        stencil_write_mask;
};

struct saved_attrib {
    int32_t enabled, size, type, normalized, stride, buffer;
    void *pointer;
};

static struct saved_server g_server[4];
static int g_server_top;
static struct saved_attrib g_client[4][ATTRIBS];
static int g_client_top;

static void c_glPushAttrib(uint32_t mask) {
    load();
    (void)mask;
    if (g_server_top >= 4) {
        return;
    }
    struct saved_server *s = &g_server[g_server_top++];
    for (size_t i = 0; i < CAP_COUNT; ++i) {
        s->caps[i] = es.IsEnabled(k_caps[i]);
    }
    s->textured = g_textured;
    es.GetIntegerv(0x80c9, &s->blend[0]); /* BLEND_SRC_RGB */
    es.GetIntegerv(0x80c8, &s->blend[1]); /* BLEND_DST_RGB */
    es.GetIntegerv(0x80cb, &s->blend[2]); /* BLEND_SRC_ALPHA */
    es.GetIntegerv(0x80ca, &s->blend[3]); /* BLEND_DST_ALPHA */
    es.GetIntegerv(0x8009, &s->equation[0]);
    es.GetIntegerv(0x883d, &s->equation[1]);
    es.GetIntegerv(0x0ba2, s->viewport);
    es.GetIntegerv(0x0c10, s->scissor);
    es.GetIntegerv(0x0b45, &s->cull);
    es.GetBooleanv(0x0c23, s->color_mask);
    es.GetBooleanv(0x0b72, &s->depth_mask);
    es.GetIntegerv(0x0b92, &s->stencil_func);
    es.GetIntegerv(0x0b97, &s->stencil_ref);
    es.GetIntegerv(0x0b93, &s->stencil_value_mask);
    es.GetIntegerv(0x0b94, &s->stencil_fail);
    es.GetIntegerv(0x0b95, &s->stencil_zfail);
    es.GetIntegerv(0x0b96, &s->stencil_zpass);
    es.GetIntegerv(0x0b98, &s->stencil_write_mask);
}

static void c_glPopAttrib(void) {
    if (g_server_top <= 0) {
        return;
    }
    const struct saved_server *s = &g_server[--g_server_top];
    for (size_t i = 0; i < CAP_COUNT; ++i) {
        (s->caps[i] ? es.Enable : es.Disable)(k_caps[i]);
    }
    g_textured = s->textured;
    es.BlendFuncSeparate((uint32_t)s->blend[0], (uint32_t)s->blend[1], (uint32_t)s->blend[2],
                         (uint32_t)s->blend[3]);
    es.BlendEquationSeparate((uint32_t)s->equation[0], (uint32_t)s->equation[1]);
    es.Viewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
    es.Scissor(s->scissor[0], s->scissor[1], s->scissor[2], s->scissor[3]);
    es.CullFace((uint32_t)s->cull);
    es.ColorMask(s->color_mask[0], s->color_mask[1], s->color_mask[2], s->color_mask[3]);
    es.DepthMask(s->depth_mask);
    es.StencilFunc((uint32_t)s->stencil_func, s->stencil_ref, (uint32_t)s->stencil_value_mask);
    es.StencilOp((uint32_t)s->stencil_fail, (uint32_t)s->stencil_zfail, (uint32_t)s->stencil_zpass);
    es.StencilMask((uint32_t)s->stencil_write_mask);
}

/* Attributes 0..2 of whatever VAO is bound: the game's vertex arrays survive the overlay. */
static void c_glPushClientAttrib(uint32_t mask) {
    load();
    (void)mask;
    if (g_client_top >= 4) {
        return;
    }
    struct saved_attrib *a = g_client[g_client_top++];
    for (uint32_t i = 0; i < ATTRIBS; ++i) {
        es.GetVertexAttribiv(i, 0x8622, &a[i].enabled);    /* ENABLED */
        es.GetVertexAttribiv(i, 0x8623, &a[i].size);       /* SIZE */
        es.GetVertexAttribiv(i, 0x8625, &a[i].type);       /* TYPE */
        es.GetVertexAttribiv(i, 0x886a, &a[i].normalized); /* NORMALIZED */
        es.GetVertexAttribiv(i, 0x8624, &a[i].stride);     /* STRIDE */
        es.GetVertexAttribiv(i, 0x889f, &a[i].buffer);     /* BUFFER_BINDING */
        es.GetVertexAttribPointerv(i, 0x8645, &a[i].pointer);
    }
}

static void c_glPopClientAttrib(void) {
    if (g_client_top <= 0) {
        return;
    }
    const struct saved_attrib *a = g_client[--g_client_top];
    int32_t array_buffer = 0;
    es.GetIntegerv(0x8894, &array_buffer);
    for (uint32_t i = 0; i < ATTRIBS; ++i) {
        es.BindBuffer(0x8892, (uint32_t)a[i].buffer);
        es.VertexAttribPointer(i, a[i].size, (uint32_t)a[i].type, (uint8_t)a[i].normalized,
                               a[i].stride, a[i].pointer);
        (a[i].enabled ? es.EnableVertexAttribArray : es.DisableVertexAttribArray)(i);
    }
    es.BindBuffer(0x8892, (uint32_t)array_buffer);
}

void *gles_compat_proc(const char *name) {
    static const struct {
        const char *name;
        void *fn;
    } table[] = {
        {"glPushAttrib", (void *)c_glPushAttrib},
        {"glPopAttrib", (void *)c_glPopAttrib},
        {"glPushClientAttrib", (void *)c_glPushClientAttrib},
        {"glPopClientAttrib", (void *)c_glPopClientAttrib},
        {"glMatrixMode", (void *)c_glMatrixMode},
        {"glPushMatrix", (void *)c_glPushMatrix},
        {"glPopMatrix", (void *)c_glPopMatrix},
        {"glLoadIdentity", (void *)c_glLoadIdentity},
        {"glOrtho", (void *)c_glOrtho},
        {"glTranslatef", (void *)c_glTranslatef},
        {"glRotatef", (void *)c_glRotatef},
        {"glBegin", (void *)c_glBegin},
        {"glEnd", (void *)c_glEnd},
        {"glVertex2f", (void *)c_glVertex2f},
        {"glTexCoord2f", (void *)c_glTexCoord2f},
        {"glColor4f", (void *)c_glColor4f},
        {"glEnable", (void *)c_glEnable},
        {"glDisable", (void *)c_glDisable},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (strcmp(table[i].name, name) == 0) {
            return table[i].fn;
        }
    }
    return NULL;
}
#endif
