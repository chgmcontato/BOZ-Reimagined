package com.carlos.boz;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.util.Log;
import java.io.InputStream;

/** Menu em quadrinhos com assets originais. Foco é desenhado pelo Cursor, em cantoneiras. */
final class CharacterMenu {
    private static final int PAPER = 0xFFF2EEE4, INK = 0xFF151211;
    static final String[] CHARACTERS = {"NIKOLAI", "TAKEO", "RICHTOFEN", "DEMPSEY"};
    private static final String[][] NAMES = {
        {"NIKOLAI", "BELINSKI"}, {"TAKEO", "MASAKI"},
        {"DR. EDWARD", "RICHTOFEN"}, {"TANK", "DEMPSEY"}
    };
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final RectF rect = new RectF();
    private final Path panel = new Path();
    private final Bitmap[] portraits = new Bitmap[4];
    private final Bitmap[] scenes = new Bitmap[5];
    private final Bitmap paper, titleStrip, back, start, tick;
    private final Typeface comic;
    private final Context m_Context;

    CharacterMenu(Context context) {
        m_Context = context;
        Typeface loaded;
        try { loaded = Typeface.createFromAsset(context.getAssets(), "character-menu/comic.ttf"); }
        catch (Exception e) { loaded = Typeface.create("sans-serif-condensed", Typeface.BOLD_ITALIC); }
        comic = loaded;
        String[] names = {"nikolai", "takeo", "richtofen", "dempsey"};
        for (int i = 0; i < names.length; i++) portraits[i] = load(context, names[i]);
        names = new String[]{"background", "arcade-background", "ascension-background", "cotd-background"};
        for (int i = 0; i < names.length; i++) scenes[i] = load(context, names[i]);
        scenes[4] = scenes[0]; // Protótipo Moon continua usando o cenário de Kino.
        paper = load(context, "paper");
        titleStrip = load(context, "title");
        back = load(context, "back");
        start = load(context, "start");
        tick = load(context, "tick");
    }

    private Bitmap load(Context context, String name) {
        try (InputStream in = context.getAssets().open("character-menu/" + name + ".png")) {
            return BitmapFactory.decodeStream(in);
        } catch (Exception e) {
            Log.e("BozPad", "Arte do menu ausente: " + name, e);
            return null;
        }
    }

    private void box(Canvas c, float x, float y, float w, float h, int color) {
        p.setStyle(Paint.Style.FILL); p.setColor(color);
        c.drawRect(x, y, x + w, y + h, p);
    }

    private void text(Canvas c, String value, float x, float y, float size, int color, boolean outline) {
        p.setTypeface(comic); p.setTextSize(size); p.setStrokeJoin(Paint.Join.ROUND);
        if (outline) {
            p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(size * 0.14f);
            p.setColor(INK); c.drawText(value, x, y, p);
        }
        p.setStyle(Paint.Style.FILL); p.setColor(color);
        c.drawText(value, x, y, p);
    }

    private void bitmap(Canvas c, Bitmap b, float x, float y, float w, float h) {
        if (b == null) return;
        p.setStyle(Paint.Style.FILL); p.setColor(0xFFFFFFFF);
        rect.set(x, y, x + w, y + h);
        c.drawBitmap(b, null, rect, p);
    }

    private void label(Canvas c, String value, float x, float y, float size) {
        c.save(); c.rotate(-3, x, y);
        text(c, value, x, y, size, INK, false);
        c.restore();
    }

    private void comicPanel(Canvas c, float x, float y, float w, float h, int color, float skew) {
        panel.reset();
        panel.moveTo(x, y + skew); panel.lineTo(x + w, y);
        panel.lineTo(x + w - 3, y + h - skew); panel.lineTo(x + 2, y + h); panel.close();
        p.setStyle(Paint.Style.FILL); p.setColor(color); c.drawPath(panel, p);
        p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(6); p.setColor(INK); c.drawPath(panel, p);
        p.setStrokeWidth(1.5f); p.setColor(0xFF9C9687); c.drawPath(panel, p);
        p.setStyle(Paint.Style.FILL);
    }

    void draw(Canvas c, int width, int height, int map, int character,
              boolean busy, boolean failed, boolean launching) {
        int selectedMap = Math.max(0, Math.min(Cursor.MAP_NAMES.length - 1, map - 1));
        c.save(); c.scale(width / 1280f, height / 720f);
        c.drawColor(INK);
        bitmap(c, paper, 0, 0, 1280, 720);
        // Recobre a ilustração da página-base; preserva o papel e suas bordas originais.
        box(c, 48, 80, 1184, 582, INK);
        bitmap(c, scenes[selectedMap], 64, 96, 1152, 556);
        box(c, 64, 96, 1152, 556, 0x63000000);

        // Tarjas de papel inclinadas e letras do próprio jogo, sem títulos de dashboard.
        bitmap(c, titleStrip, 66, 34, 742, 119);
        label(c, "SELECT CHARACTER", 103, 108, 39);
        bitmap(c, titleStrip, 837, 73, 374, 79);
        label(c, "SELECT MAP", 869, 125, 28);

        boolean arcade = map == 2;
        for (int i = 0; i < 4; i++) {
            float y = 164 + i * 100;
            bitmap(c, portraits[i], 86, y, 300, 94);
            if (arcade) box(c, 86, y, 300, 94, 0xB0151211);
            // O texto ocupa o espaço negativo de cada retrato, como na composição original.
            float x = (i == 0 || i == 3) ? 211 : 101;
            float baseline = y + ((i == 0 || i == 3) ? 42 : 29);
            text(c, NAMES[i][0], x, baseline, 23, arcade ? 0xFFAAA69D : PAPER, true);
            text(c, NAMES[i][1], x, baseline + 25, 23, arcade ? 0xFFAAA69D : PAPER, true);
            if (character == i && !arcade) bitmap(c, tick, 350, y + 3, 34, 37);
        }

        // Quadro central grande: a arte original substitui os pequenos ícones da primeira versão.
        comicPanel(c, 410, 155, 410, 399, INK, 7);
        c.save(); c.clipPath(panel);
        bitmap(c, scenes[selectedMap], 413, 155, 404, 399);
        c.restore();
        bitmap(c, titleStrip, 418, 467, 392, 76);
        label(c, Cursor.MAP_NAMES[selectedMap], 445, 515, 27);
        if (arcade) text(c, "ARCADE SQUAD", 459, 446, 23, PAPER, true);

        for (int i = 0; i < Cursor.MAP_NAMES.length; i++) {
            float y = 174 + i * 77;
            boolean selected = selectedMap == i;
            comicPanel(c, 850, y, 344, 64, selected ? 0xFF791D24 : 0xDE191614, i % 2 == 0 ? 4 : -3);
            text(c, Cursor.MAP_NAMES[i], 866, y + 41, 24, PAPER, true);
            if (selected) bitmap(c, tick, 1151, y + 13, 36, 38);
        }

        bitmap(c, back, 80, 577, 215, 94);
        text(c, "BACK", 113, 630, 33, PAPER, true);
        bitmap(c, start, 952, 577, 254, 94);
        text(c, "START", 984, 632, 38,
                busy || failed || launching ? 0xFFBCB8AD : PAPER, true);
        String status = launching ? "STARTING GAME..." : failed ? "SELECT THE MAP AGAIN" :
                busy ? "PREPARING MAP..." : "READY TO SURVIVE!";
        text(c, status, 408, 620, failed ? 22 : 25, PAPER, true);
        text(c, "PLAYER: " + PlayerNames.get(m_Context, 0), 408, 648, 19, 0xFFD5C9AC, true);
        text(c, "D-PAD: NAVIGATE     A: SELECT     B: BACK", 265, 698, 20, INK, false);
        c.restore();
    }
}
