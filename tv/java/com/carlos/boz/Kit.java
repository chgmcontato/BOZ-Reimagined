package com.carlos.boz;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.util.Log;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;

/**
 * A mesma linguagem dos menus do PC (src/menus.c, kit_*): folha rasgada, tarja, painéis, setas e explosão.
 * Coordenadas 1280x720. LAYOUT vem dos PSDs editados (src/menus_layout.h): deslocamento, escala e inclinação.
 */
final class Kit {
    static final int PAPER = 0xFFF2EEE4, INK = 0xFF151211, RED = 0xFF791D24, SAND = 0xFFD5C9AC, EDGE = 0xFF827763;
    static final int YELLOW = 0xFFF5C518, BODY = 0xFF0D0C0B, BLACK = 0xFF070606;
    static final int BX0 = 72, BY0 = 62, BX1 = 1208, BY1 = 668;
    private static final int SX0 = 44, SY0 = 14, SX1 = 1236, SY1 = 690;
    static final String HINT = "D-PAD: NAVIGATE     A: SELECT     B: BACK";
    private static final String[][] LAYOUT = {
        {"main", "title", "0.00", "13.33", "1.000", "-5.95"},
        {"main", "quit label", "-14.67", "14.00", "1.000", "0.00"},
        {"single", "back label", "-20.00", "11.33", "1.000", "0.00"},
        {"coop", "title", "-3.67", "-1.00", "1.000", "-3.44"},
        {"coop", "back label", "-24.00", "9.33", "1.000", "0.00"},
        {"coopwifi", "title", "6.67", "7.67", "1.000", "-4.62"},
        {"coopwifi", "back label", "-23.33", "12.00", "1.000", "0.00"},
        {"charselect", "takeo name 1", "35.33", "38.00", "1.000", "0.00"},
        {"charselect", "takeo name 2", "35.33", "38.00", "1.000", "0.00"},
        {"charselect", "richtofen name 1", "0.33", "29.33", "0.682", "0.00"},
        {"charselect", "richtofen name 2", "22.00", "26.00", "1.000", "0.00"},
        {"charselect", "dempsey name 1", "0.67", "27.33", "1.000", "0.00"},
        {"charselect", "dempsey name 2", "0.67", "27.33", "1.000", "0.00"},
        {"charselect", "character title", "-0.33", "2.33", "1.000", "-4.48"},
        {"charselect", "map title", "-0.33", "8.00", "1.000", "-5.09"},
        {"charselect", "back label", "-20.00", "10.00", "1.000", "0.00"},
        {"charselect", "start label", "15.00", "14.67", "1.000", "-9.91"},
        {"pause_solo", "title", "0.33", "7.67", "1.000", "-4.39"},
        {"gameover", "title", "0.00", "4.00", "1.000", "0.00"},
        {"gameover", "quit label", "-11.33", "16.67", "1.000", "0.00"},
        {"gameover", "retry label", "5.33", "19.33", "1.000", "0.00"},
        {"newgame", "back label", "-24.00", "11.33", "1.000", "0.00"},
        {"newgame", "ok label", "27.33", "9.33", "1.000", "0.00"},
        {"quitconfirm", "no label", "-37.33", "10.00", "1.000", "0.00"},
        {"quitconfirm", "yes label", "29.33", "10.00", "1.000", "0.00"},
        {"savequit", "no label", "-39.33", "10.00", "1.000", "0.00"},
        {"savequit", "yes label", "28.67", "10.00", "1.000", "0.00"},
        {"leavecoop", "no label", "-40.00", "8.00", "1.000", "0.00"},
        {"leavecoop", "yes label", "34.00", "10.00", "1.000", "0.00"},
        {"reconnectconfirm", "no label", "-30.67", "9.33", "1.000", "0.00"},
        {"reconnectconfirm", "yes label", "22.67", "10.00", "1.000", "0.00"},
        {"name_editor", "title", "-0.33", "7.33", "1.000", "-5.03"}
    };

    private static Kit s_Instance;
    static Kit get(Context c) {
        if (s_Instance == null) s_Instance = new Kit(c.getApplicationContext());
        return s_Instance;
    }

    private final Context m_Context;
    private final Map<String, Bitmap> m_Art = new HashMap<>();
    private final Typeface m_Comic;
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Path m_Panel = new Path();
    private final RectF m_Dst = new RectF();
    private Bitmap m_Sheet, m_SheetBody, m_Burst;
    String screen = "";

    private Kit(Context c) {
        m_Context = c;
        Typeface loaded;
        try { loaded = Typeface.createFromAsset(c.getAssets(), "character-menu/comic.ttf"); }
        catch (Exception e) { loaded = Typeface.create("sans-serif-condensed", Typeface.BOLD_ITALIC); }
        m_Comic = loaded;
    }

    Bitmap art(String asset) {
        if (!m_Art.containsKey(asset)) {
            Bitmap b = null;
            try (InputStream in = m_Context.getAssets().open(asset)) { b = BitmapFactory.decodeStream(in); }
            catch (Exception e) { Log.e("BozPad", "Kit: " + asset, e); }
            m_Art.put(asset, b);
        }
        return m_Art.get(asset);
    }

    void image(Canvas c, Bitmap b, float x, float y, float w, float h, int alpha) {
        if (b == null) return;
        p.setStyle(Paint.Style.FILL); p.setColor(Color.WHITE); p.setAlpha(alpha);
        m_Dst.set(x, y, x + w, y + h);
        c.drawBitmap(b, null, m_Dst, p);
        p.setAlpha(255);
    }

    /* ---- texto ---- */

    void text(Canvas c, String s, float x, float y, float size, int color, float maxWidth, boolean outline, Paint.Align align) {
        p.setTextAlign(align); p.setTypeface(m_Comic); p.setTextSize(size); p.setStrokeJoin(Paint.Join.ROUND);
        float w = p.measureText(s);
        if (w > maxWidth && w > 0) p.setTextSize(size * maxWidth / w);
        if (outline) {
            p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(p.getTextSize() * 0.12f);
            p.setColor(0xFF111111); c.drawText(s, x, y, p);
        }
        p.setStyle(Paint.Style.FILL); p.setColor(color);
        c.drawText(s, x, y, p);
        p.setTextAlign(Paint.Align.LEFT);
    }

    /** Texto posicionado à mão nos PSDs. */
    void placed(Canvas c, String layer, String s, float x, float y, float size, int color, float maxWidth, boolean outline, Paint.Align align) {
        String[] adj = null;
        for (String[] r : LAYOUT) if (r[0].equals(screen) && r[1].equals(layer)) { adj = r; break; }
        if (adj == null) { text(c, s, x, y, size, color, maxWidth, outline, align); return; }
        p.setTypeface(m_Comic); p.setTextSize(size);
        float w = p.measureText(s);
        if (w > maxWidth && w > 0) { size *= maxWidth / w; w = maxWidth; }
        float left = align == Paint.Align.CENTER ? x - w / 2 : align == Paint.Align.RIGHT ? x - w : x;
        float mid = left + w / 2 + Float.parseFloat(adj[2]);
        float scaled = size * Float.parseFloat(adj[4]);
        float base = y + Float.parseFloat(adj[3]) + (scaled - size) * 0.35f;
        c.save(); c.rotate(Float.parseFloat(adj[5]), mid, base - scaled * 0.35f);
        text(c, s, mid, base, scaled, color, 1e6f, outline, Paint.Align.CENTER);
        c.restore();
    }

    /* ---- folha ---- */

    private static float torn(int i) {
        long h = (i * 2654435761L) & 0xffffffffL;
        return (h >>> 24) % 9;
    }

    private void edge(Canvas c, float x0, float y0, float x1, float y1, float nx, float ny, int seed) {
        int n = (int) (Math.hypot(x1 - x0, y1 - y0) / 38);
        Path t = new Path();
        for (int i = 0; i < n; i++) {
            float a = i / (float) n, b = (i + 1) / (float) n, m = (a + b) / 2, d = 2 + torn(seed + i);
            t.moveTo(x0 + (x1 - x0) * a, y0 + (y1 - y0) * a);
            t.lineTo(x0 + (x1 - x0) * b, y0 + (y1 - y0) * b);
            t.lineTo(x0 + (x1 - x0) * m + nx * d, y0 + (y1 - y0) * m + ny * d);
            t.close();
        }
        c.drawPath(t, p);
    }

    private static float clamp(float v, float lo, float hi) { return Math.min(hi, Math.max(lo, v)); }

    private Bitmap buildSheet(boolean body) {
        Bitmap out = Bitmap.createBitmap(1280, 720, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(out);
        image(c, art("character-menu/paper.png"), 0, 0, 1280, 720, 255);
        p.setStyle(Paint.Style.FILL); p.setColor(BLACK);
        c.drawRect(0, 0, 1280, SY0, p); c.drawRect(0, SY1, 1280, 720, p);
        c.drawRect(0, SY0, SX0, SY1, p); c.drawRect(SX1, SY0, 1280, SY1, p);
        edge(c, SX0, SY0, SX1, SY0, 0, 1, 0);
        edge(c, SX1, SY0, SX1, SY1, -1, 0, 40);
        edge(c, SX0, SY1, SX1, SY1, 0, -1, 80);
        edge(c, SX0, SY0, SX0, SY1, 1, 0, 120);
        if (body) {
            Path b = new Path();
            b.moveTo(BX0, BY0 + 4); b.lineTo(BX1, BY0); b.lineTo(BX1 - 4, BY1 - 4); b.lineTo(BX0 + 3, BY1); b.close();
            p.setColor(BODY); c.drawPath(b, p);
            p.setColor(0xFF1D1211);
            float cx = 640, cy = 400, r = 760;
            for (int i = 0; i < 28; i++) {
                double a0 = i * 0.2244, a1 = a0 + 0.11;
                Path t = new Path();
                t.moveTo(cx, cy);
                t.lineTo(clamp((float) (cx + r * Math.cos(a1)), BX0 + 4, BX1 - 4), clamp((float) (cy + r * Math.sin(a1)), BY0 + 6, BY1 - 4));
                t.lineTo(clamp((float) (cx + r * Math.cos(a0)), BX0 + 4, BX1 - 4), clamp((float) (cy + r * Math.sin(a0)), BY0 + 6, BY1 - 4));
                t.close();
                c.drawPath(t, p);
            }
        }
        return out;
    }

    /** Em cache: a folha é cara de redesenhar e não muda. */
    void sheet(Canvas c) {
        if (m_SheetBody == null) m_SheetBody = buildSheet(true);
        image(c, m_SheetBody, 0, 0, 1280, 720, 255);
    }

    void burst(Canvas c) {
        if (m_Burst == null) {
            m_Burst = Bitmap.createBitmap(1280, 720, Bitmap.Config.ARGB_8888);
            Canvas b = new Canvas(m_Burst);
            b.drawColor(0xFF0A0807);
            p.setStyle(Paint.Style.FILL);
            for (int i = 0; i < 16; i++) {
                double a0 = i * 0.3927 + 0.08, a1 = a0 + 0.2 + 0.05 * (i % 3);
                Path t = new Path();
                t.moveTo(640, 360);
                t.lineTo((float) (640 + 900 * Math.cos(a0)), (float) (360 + 900 * Math.sin(a0)));
                t.lineTo((float) (640 + 900 * Math.cos(a1)), (float) (360 + 900 * Math.sin(a1)));
                t.close();
                p.setColor(i % 2 == 1 ? 0xFF6B3A1C : 0xFF8A4D25);
                b.drawPath(t, p);
            }
            p.setColor(0x40000000); b.drawRect(0, 0, 1280, 720, p);
        }
        image(c, m_Burst, 0, 0, 1280, 720, 255);
    }

    /* ---- componentes ---- */

    void strip(Canvas c, float x, float y, float w, String title, String layer) {
        float h = w / 4;
        image(c, art("character-menu/title.png"), x, y, w, h, 255);
        placed(c, layer, title, x + w * 0.09f, y + h * 0.45f, Math.min(h * 0.42f, 44), INK, w * 0.80f, false, Paint.Align.LEFT);
    }

    void pageTitle(Canvas c, String title) {
        strip(c, 60, 8, 520, title, "title");
        text(c, "BLACK OPS ZOMBIES", 1188, 112, 26, SAND, 560, true, Paint.Align.RIGHT);
        text(c, "REIMAGINED", 1188, 152, 38, PAPER, 560, true, Paint.Align.RIGHT);
    }

    void hint(Canvas c, String s) {
        text(c, s, 640, 711, 18, SAND, 1000, false, Paint.Align.CENTER);
    }

    void arrow(Canvas c, boolean back, RectF r, String label, String id, boolean dim) {
        float iw = Math.min(r.width(), Math.max(r.height() * 2.2f, 240)), ih = iw / 2;
        float ix = r.left + (r.width() - iw) / 2, iy = r.bottom - ih + 8;
        image(c, art(back ? "character-menu/back.png" : "character-menu/start.png"), ix, iy, iw, ih, dim ? 140 : 255);
        placed(c, id + " label", label, ix + iw * (back ? 0.56f : 0.46f), iy + ih * 0.48f, Math.min(ih * 0.36f, 38),
                dim ? 0xFFBCB8AD : PAPER, iw * 0.72f, true, Paint.Align.CENTER);
    }

    void comicPath(Path path, RectF r, float skew) {
        path.reset();
        path.moveTo(r.left, r.top + skew); path.lineTo(r.right, r.top);
        path.lineTo(r.right - 3, r.bottom - skew); path.lineTo(r.left + 2, r.bottom); path.close();
    }

    void plate(Canvas c, RectF r, float skew, int fill) {
        comicPath(m_Panel, r, skew);
        p.setStyle(Paint.Style.FILL); p.setColor(fill); c.drawPath(m_Panel, p);
        frame(c, EDGE);
    }

    private void frame(Canvas c, int inner) {
        p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(5); p.setColor(INK); c.drawPath(m_Panel, p);
        p.setStrokeWidth(1.5f); p.setColor(inner); c.drawPath(m_Panel, p);
        p.setStyle(Paint.Style.FILL);
    }

    /** artSide > 0: tamanho e topo fixos da arte quadrada (ajuste vindo do PSD). */
    void panel(Canvas c, RectF r, Bitmap art, String title, String caption, boolean selected, boolean dim, float artSide, float artTop) {
        comicPath(m_Panel, r, r.height() > 100 ? 6 : 4);
        c.save(); c.clipPath(m_Panel);
        p.setStyle(Paint.Style.FILL); p.setColor(INK); c.drawRect(r, p);
        if (art != null) {
            if (art.getWidth() == art.getHeight()) {
                float side = artSide > 0 ? artSide : Math.max(r.width(), r.height()) * 0.86f;
                float top = artSide > 0 ? artTop : r.top + (r.height() - side) * 0.35f;
                image(c, art, r.centerX() - side / 2, top, side, side, 255);
            } else {
                float k = Math.max(r.width() / art.getWidth(), r.height() / art.getHeight());
                float w = art.getWidth() * k, h = art.getHeight() * k;
                image(c, art, r.centerX() - w / 2, r.centerY() - h / 2, w, h, 255);
            }
            if (dim) { p.setColor(0x58000000); c.drawRect(r, p); }
            p.setColor(0x70000000); c.drawRect(r.left, r.top + r.height() * 0.62f, r.right, r.bottom, p);
        }
        c.restore();
        boolean small = r.height() <= 100, cap = caption != null && !caption.isEmpty();
        float size = small ? Math.min(30, r.height() * 0.44f) : Math.min(40, r.width() * 0.16f);
        float base = small ? r.centerY() + size * 0.36f : r.bottom - (cap ? 48 : 22);
        text(c, title, r.left + 18, base, size, selected ? YELLOW : PAPER, r.width() - 36, true, Paint.Align.LEFT);
        if (cap) text(c, caption, r.left + 18, r.bottom - 18, 20, SAND, r.width() - 36, true, Paint.Align.LEFT);
        comicPath(m_Panel, r, r.height() > 100 ? 6 : 4);
        frame(c, selected ? RED : EDGE);
    }

    /** Arte recortada ao retângulo, sem vazar sobre a folha. */
    void cover(Canvas c, Bitmap b, RectF r, int shade) {
        if (b == null) return;
        float aspect = r.width() / r.height(), ia = b.getWidth() / (float) b.getHeight();
        android.graphics.Rect src;
        if (ia > aspect) {
            int w = Math.round(b.getHeight() * aspect);
            src = new android.graphics.Rect((b.getWidth() - w) / 2, 0, (b.getWidth() + w) / 2, b.getHeight());
        } else {
            int h = Math.round(b.getWidth() / aspect), t = Math.round((b.getHeight() - h) * 0.4f);
            src = new android.graphics.Rect(0, t, b.getWidth(), t + h);
        }
        p.setColor(Color.WHITE); c.drawBitmap(b, src, r, p);
        p.setColor(shade); c.drawRect(r, p);
    }
}
