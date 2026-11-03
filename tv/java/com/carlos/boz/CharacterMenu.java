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

    /** Cunhas quadradas que se encaixam na diagonal: y, x e baseline do nome. */
    private static final float[][] SLOTS = {{74, 214, 108}, {150, 106, 236}, {288, 106, 326}, {364, 218, 468}};
    private static final String[] IDS = {"nikolai", "takeo", "richtofen", "dempsey"};
    private static final String[] MAP_ART = {"kino", "deadops", "ascension", "cotd"};

    void draw(Canvas c, int width, int height, int map, int character,
              boolean busy, boolean failed, boolean launching) {
        int selected = Math.max(0, Math.min(3, map - 1));
        boolean arcade = map == 2;
        Kit k = Kit.get(m_Context);
        k.screen = "charselect";
        c.save(); c.scale(width / 1280f, height / 720f);
        k.sheet(c);
        for (int i = 3; i >= 0; i--) k.image(c, portraits[i], 86, SLOTS[i][0], 290, 190, arcade ? 90 : 255);
        for (int i = 0; i < 4; i++) {
            int color = arcade ? 0xFFAAA69D : character == i ? Kit.YELLOW : Kit.PAPER;
            k.placed(c, IDS[i] + " name 1", NAMES[i][0], SLOTS[i][1], SLOTS[i][2], 23, color, 170, true, Paint.Align.LEFT);
            k.placed(c, IDS[i] + " name 2", NAMES[i][1], SLOTS[i][1], SLOTS[i][2] + 25, 23, color, 170, true, Paint.Align.LEFT);
            if (character == i && !arcade) k.image(c, tick, SLOTS[i][1] + 112, SLOTS[i][2] - 2, 38, 38, 255);
        }
        for (int i = 0; i < 4; i++) {
            float x = 400 + i * 202, y = 112 - (i % 2 == 1 ? 4 : 0);
            k.panel(c, new RectF(x, y, x + 196, y + 330), k.art("character-menu/" + MAP_ART[i] + ".png"),
                    Cursor.MAP_NAMES[i], "", selected == i, selected != i, 0, 0);
            if (selected == i) k.image(c, tick, x + 144, y + 10, 42, 42, 255);
        }
        k.strip(c, 58, 4, 470, "SELECT CHARACTER", "character title");
        k.strip(c, 700, 2, 400, "SELECT MAP", "map title");
        boolean blocked = busy || failed || launching;
        k.arrow(c, true, new RectF(76, 548, 316, 664), "BACK", "back", false);
        k.arrow(c, false, new RectF(960, 548, 1200, 664), "START", "start", blocked);
        String status = launching ? "STARTING GAME..." : failed ? "SELECT THE MAP AGAIN"
                : busy ? "PREPARING MAP..." : arcade ? "ARCADE SQUAD" : "READY TO SURVIVE!";
        k.text(c, status, 640, 590, 38, Kit.PAPER, 560, true, Paint.Align.CENTER);
        k.text(c, "PLAYER: " + PlayerNames.get(m_Context, 0), 640, 628, 24, Kit.SAND, 560, true, Paint.Align.CENTER);
        k.hint(c, Kit.HINT);
        c.restore();
    }
}
