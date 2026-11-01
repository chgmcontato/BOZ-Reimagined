#include "ui.h"
#include "host.h"

#include <SDL.h>
#include <math.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../third_party/stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"

enum {
    GL_TEXTURE_2D_ = 0x0de1,
    GL_QUADS_ = 0x0007,
    GL_RGBA_ = 0x1908,
    GL_ALPHA_ = 0x1906,
    GL_UNSIGNED_BYTE_ = 0x1401,
    GL_LINEAR_ = 0x2601,
    GL_TEXTURE_MIN_FILTER_ = 0x2801,
    GL_TEXTURE_MAG_FILTER_ = 0x2800,
    FONT_PX = 56,
    ATLAS = 1024,
    FIRST_CHAR = 32,
    CHAR_COUNT = 224,
};

static struct {
    void (*PushAttrib)(uint32_t);
    void (*PopAttrib)(void);
    void (*PushClientAttrib)(uint32_t);
    void (*PopClientAttrib)(void);
    void (*MatrixMode)(uint32_t);
    void (*PushMatrix)(void);
    void (*PopMatrix)(void);
    void (*LoadIdentity)(void);
    void (*Ortho)(double, double, double, double, double, double);
    void (*Disable)(uint32_t);
    void (*Enable)(uint32_t);
    void (*UseProgram)(uint32_t);
    void (*Begin)(uint32_t);
    void (*End)(void);
    void (*Vertex2f)(float, float);
    void (*TexCoord2f)(float, float);
    void (*Color4f)(float, float, float, float);
    void (*Viewport)(int, int, int, int);
    void (*BlendFunc)(uint32_t, uint32_t);
    void (*BindFramebuffer)(uint32_t, uint32_t);
    void (*BindBuffer)(uint32_t, uint32_t);
    void (*BindTexture)(uint32_t, uint32_t);
    void (*GenTextures)(int, uint32_t *);
    void (*DeleteTextures)(int, const uint32_t *);
    void (*TexImage2D)(uint32_t, int, int, int, int, int, uint32_t, uint32_t, const void *);
    void (*TexParameteri)(uint32_t, uint32_t, int);
    void (*PixelStorei)(uint32_t, int);
    void (*ActiveTexture)(uint32_t);
    void (*GetIntegerv)(uint32_t, int32_t *);
    void (*BindVertexArray)(uint32_t);
    void (*StencilFunc)(uint32_t, int, uint32_t);
    void (*StencilOp)(uint32_t, uint32_t, uint32_t);
    void (*StencilMask)(uint32_t);
    void (*ColorMask)(uint8_t, uint8_t, uint8_t, uint8_t);
    void (*ClearStencil)(int);
    void (*Clear)(uint32_t);
} gl;

struct font {
    stbtt_bakedchar chars[CHAR_COUNT];
    uint32_t tex;
    bool ok;
};
static struct font g_fonts[UI_FONT_COUNT];
static int g_font_slot = UI_FONT_DISPLAY;
#define g_chars (g_fonts[g_font_slot].chars)
#define g_font_tex (g_fonts[g_font_slot].tex)
#define g_font_ok (g_fonts[g_font_slot].ok)
static float g_ref_w = 1280.0f, g_ref_h = 720.0f;
static int g_width, g_height;
static float g_scale, g_off_x, g_off_y;
static int32_t g_saved_program, g_saved_fb, g_saved_tex, g_saved_active, g_saved_vao, g_saved_abuf;

#define LOAD(field, name) *(void **)&gl.field = gl_proc(name)

static const char *const k_system_fonts[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
    "C:/Windows/Fonts/arialbd.ttf",
    NULL,
};

void ui_init(void) {
    LOAD(PushAttrib, "glPushAttrib");
    LOAD(PopAttrib, "glPopAttrib");
    LOAD(PushClientAttrib, "glPushClientAttrib");
    LOAD(PopClientAttrib, "glPopClientAttrib");
    LOAD(MatrixMode, "glMatrixMode");
    LOAD(PushMatrix, "glPushMatrix");
    LOAD(PopMatrix, "glPopMatrix");
    LOAD(LoadIdentity, "glLoadIdentity");
    LOAD(Ortho, "glOrtho");
    LOAD(Disable, "glDisable");
    LOAD(Enable, "glEnable");
    LOAD(UseProgram, "glUseProgram");
    LOAD(Begin, "glBegin");
    LOAD(End, "glEnd");
    LOAD(Vertex2f, "glVertex2f");
    LOAD(TexCoord2f, "glTexCoord2f");
    LOAD(Color4f, "glColor4f");
    LOAD(Viewport, "glViewport");
    LOAD(BlendFunc, "glBlendFunc");
    LOAD(BindFramebuffer, "glBindFramebuffer");
    LOAD(BindBuffer, "glBindBuffer");
    LOAD(BindTexture, "glBindTexture");
    LOAD(GenTextures, "glGenTextures");
    LOAD(DeleteTextures, "glDeleteTextures");
    LOAD(TexImage2D, "glTexImage2D");
    LOAD(TexParameteri, "glTexParameteri");
    LOAD(PixelStorei, "glPixelStorei");
    LOAD(ActiveTexture, "glActiveTexture");
    LOAD(GetIntegerv, "glGetIntegerv");
    LOAD(BindVertexArray, "glBindVertexArray");
    LOAD(StencilFunc, "glStencilFunc");
    LOAD(StencilOp, "glStencilOp");
    LOAD(StencilMask, "glStencilMask");
    LOAD(ColorMask, "glColorMask");
    LOAD(ClearStencil, "glClearStencil");
    LOAD(Clear, "glClear");
    for (int slot = 0; slot < UI_FONT_COUNT; ++slot) {
        g_font_slot = slot;
        for (int i = 0; k_system_fonts[i] && !g_font_ok; ++i) {
            ui_load_font(k_system_fonts[i]);
        }
    }
    g_font_slot = UI_FONT_DISPLAY;
}

void ui_font(int slot) {
    g_font_slot = slot;
}

bool ui_load_font_slot(int slot, const char *ttf_path) {
    int saved = g_font_slot;
    g_font_slot = slot;
    bool ok = ui_load_font(ttf_path);
    g_font_slot = saved;
    return ok;
}

void ui_set_reference(float w, float h) {
    g_ref_w = w;
    g_ref_h = h;
}

static uint8_t *read_all(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = len > 0 ? malloc((size_t)len) : NULL;
    if (data && fread(data, 1, (size_t)len, f) != (size_t)len) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = (size_t)len;
    return data;
}

static uint32_t make_texture(int w, int h, uint32_t format, const void *pixels) {
    uint32_t tex = 0;
    int32_t prev = 0;
    gl.GetIntegerv(0x8069, &prev); /* GL_TEXTURE_BINDING_2D */
    gl.GenTextures(1, &tex);
    gl.BindTexture(GL_TEXTURE_2D_, tex);
    gl.PixelStorei(0x0cf5, 1); /* GL_UNPACK_ALIGNMENT */
    /* mipmapped: text and art are drawn well below their baked size */
    gl.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, 0x2703); /* LINEAR_MIPMAP_LINEAR */
    gl.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_LINEAR_);
    gl.TexParameteri(GL_TEXTURE_2D_, 0x8191, 1);                       /* GENERATE_MIPMAP */
    gl.TexImage2D(GL_TEXTURE_2D_, 0, (int)format, w, h, 0, format, GL_UNSIGNED_BYTE_, pixels);
    gl.BindTexture(GL_TEXTURE_2D_, (uint32_t)prev);
    return tex;
}

bool ui_load_font(const char *ttf_path) {
    size_t size = 0;
    uint8_t *ttf = read_all(ttf_path, &size);
    if (!ttf) {
        return false;
    }
    uint8_t *bitmap = calloc(ATLAS, ATLAS);
    int rc = stbtt_BakeFontBitmap(ttf, 0, FONT_PX, bitmap, ATLAS, ATLAS, FIRST_CHAR, CHAR_COUNT,
                                  g_chars);
    free(ttf);
    if (rc == 0) {
        free(bitmap);
        return false;
    }
    if (g_font_tex) {
        gl.DeleteTextures(1, &g_font_tex);
    }
    g_font_tex = make_texture(ATLAS, ATLAS, GL_ALPHA_, bitmap);
    free(bitmap);
    g_font_ok = true;
    log_debug("font %s", ttf_path);
    return true;
}

bool ui_image_load(struct ui_image *image, const char *path) {
    int w, h, n;
    stbi_uc *pixels = stbi_load(path, &w, &h, &n, 4);
    if (!pixels) {
        image->tex = 0;
        return false;
    }
    image->tex = make_texture(w, h, GL_RGBA_, pixels);
    image->w = w;
    image->h = h;
    stbi_image_free(pixels);
    return true;
}

void ui_image_free(struct ui_image *image) {
    if (image->tex) {
        gl.DeleteTextures(1, &image->tex);
        image->tex = 0;
    }
}

bool ui_capture_coop_players(struct ui_image *image) {
    extern uint32_t video_game_fbo(void);
    if(video_width()!=1280 || video_height()!=720)return false;
    void (*read_pixels)(int,int,int,int,uint32_t,uint32_t,void*)=NULL;
    *(void **)&read_pixels=gl_proc("glReadPixels");
    if(!read_pixels)return false;
    /* present players only; card geometry measured on the native pause frame (1280x720):
     * portrait x~878, the score ends at x~1218, players 167 px apart starting at y~26 */
    enum {CX=872,CW=408,CH=120,GAP=18}; /* to the screen edge: everything the game can draw */
    uint8_t *cards=malloc(4*CW*CH*4), *panel=calloc((CW*2+GAP)*(CH*2+GAP),4);
    if(!cards || !panel){free(cards);free(panel);return false;}
    int32_t previous=0,pack=4;
    gl.GetIntegerv(0x8caa,&previous);gl.GetIntegerv(0x0d05,&pack);
    gl.BindFramebuffer(0x8ca8,video_game_fbo());gl.PixelStorei(0x0d05,1);
    static const int tops[]={26,193,360,527};
    int n=0;
#define CARD_PX(card,x,y) ((card)+((CH-1-(y))*CW+(x))*4) /* GL rows are bottom-up */
    for(int i=0;i<4;i++) {
        uint8_t *card=cards+n*CW*CH*4;
        read_pixels(CX,720-tops[i]-CH,CW,CH,GL_RGBA_,GL_UNSIGNED_BYTE_,card);
        int white=0; /* occupied = white "SCORE:" text */
        for(int y=38;y<64;y++) for(int x=128;x<238;x++) {
            const uint8_t *p=CARD_PX(card,x,y);
            white+=p[0]>190&&p[1]>190&&p[2]>190;
        }
        if(white>30) n++;
    }
    int cols=n<2?n:2, rows=(n+1)/2, W=cols*CW+(cols-1)*GAP, H=rows*CH+(rows-1)*GAP;
    for(int k=0;k<n;k++) {
        int dx=(k%2)*(CW+GAP),dy=(k/2)*(CH+GAP);
        for(int y=0;y<CH;y++) {
            uint8_t *dst=panel+((dy+y)*W+dx)*4;
            memcpy(dst,CARD_PX(cards+k*CW*CH*4,0,y),CW*4);
            for(int x=0;x<CW;x++) dst[x*4+3]=255;
        }
    }
#undef CARD_PX
    free(cards);
    gl.BindFramebuffer(0x8ca8,(uint32_t)previous);gl.PixelStorei(0x0d05,pack);
    ui_image_free(image);
    if(n>0){image->tex=make_texture(W,H,GL_RGBA_,panel);image->w=W;image->h=H;}
    free(panel);return image->tex!=0;
}

float ui_scale(void) {
    return g_scale;
}

void ui_window_to_ref(int wx, int wy, float *rx, float *ry) {
    extern SDL_Window *video_window(void);
    int ww = 1, wh = 1;
    SDL_GetWindowSize(video_window(), &ww, &wh);
    float px = (float)wx * (float)g_width / (float)ww;
    float py = (float)wy * (float)g_height / (float)wh;
    *rx = (px - g_off_x) / g_scale; /* reference units */
    *ry = (py - g_off_y) / g_scale;
}

void ui_begin(int width, int height) {
    g_width = width;
    g_height = height;
    g_scale = (float)width / g_ref_w < (float)height / g_ref_h ? (float)width / g_ref_w
                                                                 : (float)height / g_ref_h;
    g_off_x = ((float)width - g_ref_w * g_scale) / 2.0f;
    g_off_y = ((float)height - g_ref_h * g_scale) / 2.0f;

    gl.GetIntegerv(0x8b8d, &g_saved_program); /* GL_CURRENT_PROGRAM */
    gl.GetIntegerv(0x8ca6, &g_saved_fb);      /* GL_FRAMEBUFFER_BINDING */
    gl.GetIntegerv(0x84e0, &g_saved_active);  /* GL_ACTIVE_TEXTURE */
    gl.ActiveTexture(0x84c0);
    gl.GetIntegerv(0x8069, &g_saved_tex);
    gl.GetIntegerv(0x85b5, &g_saved_vao);  /* GL_VERTEX_ARRAY_BINDING */
    gl.GetIntegerv(0x8894, &g_saved_abuf); /* GL_ARRAY_BUFFER_BINDING */
    gl.PushAttrib(0x000fffffu);
    gl.PushClientAttrib(0xffffffffu);
    gl.UseProgram(0);
    gl.BindVertexArray(0);
    gl.BindBuffer(0x8892, 0);
    gl.BindFramebuffer(0x8d40, 0);
    gl.Viewport(0, 0, width, height);
    static const uint32_t off[] = {0x0b71, 0x0c11, 0x0b44, 0x0bc0, 0x0b50, 0x0b60, 0x0b90};
    for (size_t i = 0; i < sizeof(off) / sizeof(off[0]); ++i) {
        gl.Disable(off[i]);
    }
    gl.Enable(0x0be2);
    gl.BlendFunc(0x0302, 0x0303);
    gl.MatrixMode(0x1702); /* TEXTURE */
    gl.PushMatrix();
    gl.LoadIdentity();
    gl.MatrixMode(0x1701);
    gl.PushMatrix();
    gl.LoadIdentity();
    gl.Ortho(0, width, height, 0, -1, 1);
    gl.MatrixMode(0x1700);
    gl.PushMatrix();
    gl.LoadIdentity();
}

void ui_end(void) {
    gl.MatrixMode(0x1700);
    gl.PopMatrix();
    gl.MatrixMode(0x1701);
    gl.PopMatrix();
    gl.MatrixMode(0x1702);
    gl.PopMatrix();
    gl.PopClientAttrib();
    gl.PopAttrib();
    gl.BindTexture(GL_TEXTURE_2D_, (uint32_t)g_saved_tex);
    gl.ActiveTexture((uint32_t)g_saved_active);
    gl.BindBuffer(0x8892, (uint32_t)g_saved_abuf);
    gl.BindVertexArray((uint32_t)g_saved_vao);
    gl.BindFramebuffer(0x8d40, (uint32_t)g_saved_fb);
    gl.UseProgram((uint32_t)g_saved_program);
}

static float X(float x) {
    return g_off_x + x * g_scale;
}

static float Y(float y) {
    return g_off_y + y * g_scale;
}

static void color(uint32_t argb, float alpha) {
    gl.Color4f((float)((argb >> 16) & 255) / 255.0f, (float)((argb >> 8) & 255) / 255.0f,
               (float)(argb & 255) / 255.0f, (float)(argb >> 24) / 255.0f * alpha);
}

void ui_rect(float x, float y, float w, float h, uint32_t argb) {
    gl.Disable(GL_TEXTURE_2D_);
    color(argb, 1.0f);
    gl.Begin(GL_QUADS_);
    gl.Vertex2f(X(x), Y(y));
    gl.Vertex2f(X(x + w), Y(y));
    gl.Vertex2f(X(x + w), Y(y + h));
    gl.Vertex2f(X(x), Y(y + h));
    gl.End();
}

void ui_frame(float x, float y, float w, float h, float t, uint32_t argb) {
    ui_rect(x, y, w, t, argb);
    ui_rect(x, y + h - t, w, t, argb);
    ui_rect(x, y, t, h, argb);
    ui_rect(x + w - t, y, t, h, argb);
}

void ui_image(const struct ui_image *image, float x, float y, float w, float h, float alpha) {
    if (!image->tex) {
        return;
    }
    gl.Enable(GL_TEXTURE_2D_);
    gl.BindTexture(GL_TEXTURE_2D_, image->tex);
    color(0xffffffffu, alpha);
    gl.Begin(GL_QUADS_);
    gl.TexCoord2f(0, 0);
    gl.Vertex2f(X(x), Y(y));
    gl.TexCoord2f(1, 0);
    gl.Vertex2f(X(x + w), Y(y));
    gl.TexCoord2f(1, 1);
    gl.Vertex2f(X(x + w), Y(y + h));
    gl.TexCoord2f(0, 1);
    gl.Vertex2f(X(x), Y(y + h));
    gl.End();
    gl.Disable(GL_TEXTURE_2D_);
}

/* UTF-8 -> Latin-1 code point (the baked range), '?' otherwise. */
static int next_char(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    int c = s[0];
    if (c < 0x80) {
        *p += 1;
        return c;
    }
    if ((c & 0xe0) == 0xc0 && s[1]) {
        *p += 2;
        int cp = ((c & 0x1f) << 6) | (s[1] & 0x3f);
        return cp < 256 ? cp : '?';
    }
    *p += 1;
    while ((**p & 0xc0) == 0x80) {
        *p += 1;
    }
    return '?';
}

float ui_text_width(const char *text, float size) {
    if (!g_font_ok) {
        return 0;
    }
    float k = size / FONT_PX, w = 0;
    for (const char *p = text; *p;) {
        int c = next_char(&p);
        if (c >= FIRST_CHAR && c < FIRST_CHAR + CHAR_COUNT) {
            w += g_chars[c - FIRST_CHAR].xadvance * k;
        }
    }
    return w;
}

static void glyphs(const char *text, float x, float y, float k) {
    float pen = 0;
    for (const char *p = text; *p;) {
        int c = next_char(&p);
        if (c < FIRST_CHAR || c >= FIRST_CHAR + CHAR_COUNT) {
            continue;
        }
        const stbtt_bakedchar *b = &g_chars[c - FIRST_CHAR];
        float x0 = x + (pen + b->xoff * k) * g_scale, y0 = y + b->yoff * k * g_scale;
        float x1 = x0 + (float)(b->x1 - b->x0) * k * g_scale;
        float y1 = y0 + (float)(b->y1 - b->y0) * k * g_scale;
        gl.TexCoord2f((float)b->x0 / ATLAS, (float)b->y0 / ATLAS);
        gl.Vertex2f(x0, y0);
        gl.TexCoord2f((float)b->x1 / ATLAS, (float)b->y0 / ATLAS);
        gl.Vertex2f(x1, y0);
        gl.TexCoord2f((float)b->x1 / ATLAS, (float)b->y1 / ATLAS);
        gl.Vertex2f(x1, y1);
        gl.TexCoord2f((float)b->x0 / ATLAS, (float)b->y1 / ATLAS);
        gl.Vertex2f(x0, y1);
        pen += b->xadvance * k;
    }
}

static float text_impl(const char *text, float x, float baseline, float size, uint32_t argb,
                       enum ui_align align, bool outline);

float ui_text(const char *text, float x, float baseline, float size, uint32_t argb,
              enum ui_align align) {
    return text_impl(text, x, baseline, size, argb, align, true);
}

float ui_label(const char *text, float x, float baseline, float size, uint32_t argb,
               enum ui_align align) {
    return text_impl(text, x, baseline, size, argb, align, false);
}

static float text_impl(const char *text, float x, float baseline, float size, uint32_t argb,
                       enum ui_align align, bool outline) {
    if (!g_font_ok || !text || !*text) {
        return 0;
    }
    float w = ui_text_width(text, size);
    if (align == UI_CENTER) {
        x -= w / 2;
    } else if (align == UI_RIGHT) {
        x -= w;
    }
    float k = size / FONT_PX, sx = X(x), sy = Y(baseline);
    float o = size * 0.06f * g_scale;
    if (o < 1.0f) {
        o = 1.0f;
    }
    gl.Enable(GL_TEXTURE_2D_);
    gl.BindTexture(GL_TEXTURE_2D_, g_font_tex);
    gl.Begin(GL_QUADS_);
    if (outline) {
        color(0xff111111u, (float)(argb >> 24) / 255.0f);
        static const float dirs[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1},
                                         {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
        for (int i = 0; i < 8; ++i) {
            glyphs(text, sx + dirs[i][0] * o, sy + dirs[i][1] * o, k);
        }
    }
    color(argb, 1.0f);
    glyphs(text, sx, sy, k);
    gl.End();
    gl.Disable(GL_TEXTURE_2D_);
    return w;
}

/* Rounded rectangle: a centre cross plus four quarter discs. */
void ui_round_rect(float x, float y, float w, float h, float r, uint32_t argb) {
    if (r * 2 > w) {
        r = w / 2;
    }
    if (r * 2 > h) {
        r = h / 2;
    }
    ui_rect(x + r, y, w - 2 * r, h, argb);
    ui_rect(x, y + r, r, h - 2 * r, argb);
    ui_rect(x + w - r, y + r, r, h - 2 * r, argb);
    gl.Disable(GL_TEXTURE_2D_);
    color(argb, 1.0f);
    const float cx[4] = {x + r, x + w - r, x + w - r, x + r};
    const float cy[4] = {y + r, y + r, y + h - r, y + h - r};
    const float start[4] = {3.14159265f, 4.71238898f, 0.0f, 1.57079633f};
    for (int c = 0; c < 4; ++c) {
        gl.Begin(0x0006); /* TRIANGLE_FAN */
        gl.Vertex2f(X(cx[c]), Y(cy[c]));
        for (int i = 0; i <= 8; ++i) {
            float a = start[c] + 1.57079633f * (float)i / 8.0f;
            gl.Vertex2f(X(cx[c] + r * cosf(a)), Y(cy[c] + r * sinf(a)));
        }
        gl.End();
    }
}

void ui_line(float x0, float y0, float x1, float y1, float width, uint32_t argb) {
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len <= 0) {
        return;
    }
    float nx = -dy / len * width / 2, ny = dx / len * width / 2;
    gl.Disable(GL_TEXTURE_2D_);
    color(argb, 1.0f);
    gl.Begin(GL_QUADS_);
    gl.Vertex2f(X(x0 + nx), Y(y0 + ny));
    gl.Vertex2f(X(x1 + nx), Y(y1 + ny));
    gl.Vertex2f(X(x1 - nx), Y(y1 - ny));
    gl.Vertex2f(X(x0 - nx), Y(y0 - ny));
    gl.End();
}

void ui_triangle(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t argb) {
    gl.Disable(GL_TEXTURE_2D_);
    color(argb, 1.0f);
    gl.Begin(0x0004); /* TRIANGLES */
    gl.Vertex2f(X(x0), Y(y0));
    gl.Vertex2f(X(x1), Y(y1));
    gl.Vertex2f(X(x2), Y(y2));
    gl.End();
}

void ui_circle(float cx, float cy, float r, uint32_t argb) {
    gl.Disable(GL_TEXTURE_2D_);
    color(argb, 1.0f);
    gl.Begin(0x0006);
    gl.Vertex2f(X(cx), Y(cy));
    for (int i = 0; i <= 24; ++i) {
        float a = 6.2831853f * (float)i / 24.0f;
        gl.Vertex2f(X(cx + r * cosf(a)), Y(cy + r * sinf(a)));
    }
    gl.End();
}

void ui_image_part(const struct ui_image *image, float x, float y, float w, float h, float u0,
                   float v0, float u1, float v1, float alpha) {
    if (!image->tex) {
        return;
    }
    gl.Enable(GL_TEXTURE_2D_);
    gl.BindTexture(GL_TEXTURE_2D_, image->tex);
    color(0xffffffffu, alpha);
    gl.Begin(GL_QUADS_);
    gl.TexCoord2f(u0, v0);
    gl.Vertex2f(X(x), Y(y));
    gl.TexCoord2f(u1, v0);
    gl.Vertex2f(X(x + w), Y(y));
    gl.TexCoord2f(u1, v1);
    gl.Vertex2f(X(x + w), Y(y + h));
    gl.TexCoord2f(u0, v1);
    gl.Vertex2f(X(x), Y(y + h));
    gl.End();
    gl.Disable(GL_TEXTURE_2D_);
}

/* Scale to cover the box, centred (never stretched). */
void ui_image_cover(const struct ui_image *image, float x, float y, float w, float h, float alpha) {
    if (!image->tex) {
        return;
    }
    float s = w / (float)image->w > h / (float)image->h ? w / (float)image->w : h / (float)image->h;
    float iw = (float)image->w * s, ih = (float)image->h * s;
    ui_image(image, x + (w - iw) / 2, y + (h - ih) / 2, iw, ih, alpha);
}

void ui_quad(const float pts[8], uint32_t argb) {
    gl.Disable(GL_TEXTURE_2D_);
    color(argb, 1.0f);
    gl.Begin(GL_QUADS_);
    for (int i = 0; i < 4; ++i) {
        gl.Vertex2f(X(pts[i * 2]), Y(pts[i * 2 + 1]));
    }
    gl.End();
}

void ui_quad_outline(const float pts[8], float width, uint32_t argb) {
    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        ui_line(pts[i * 2], pts[i * 2 + 1], pts[j * 2], pts[j * 2 + 1], width, argb);
        ui_circle(pts[i * 2], pts[i * 2 + 1], width / 2, argb);
    }
}

/* Stencil clipping: shapes drawn between mask_begin and mask_use form the mask. */
void ui_mask_begin(void) {
    gl.Enable(0x0b90); /* STENCIL_TEST */
    gl.StencilMask(0xff);
    gl.ClearStencil(0);
    gl.Clear(0x400); /* STENCIL_BUFFER_BIT */
    gl.StencilFunc(0x0207, 1, 0xff);         /* ALWAYS */
    gl.StencilOp(0x1e00, 0x1e00, 0x1e01);    /* KEEP KEEP REPLACE */
    gl.ColorMask(0, 0, 0, 0);
}

void ui_mask_use(bool inside) {
    gl.ColorMask(1, 1, 1, 1);
    gl.StencilFunc(inside ? 0x0202 : 0x0205, 1, 0xff); /* EQUAL / NOTEQUAL */
    gl.StencilOp(0x1e00, 0x1e00, 0x1e00);
}

void ui_mask_end(void) {
    gl.Disable(0x0b90);
}
