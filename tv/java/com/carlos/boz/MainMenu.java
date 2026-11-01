package com.carlos.boz;

import android.content.Context;
import android.graphics.*;
import android.util.Log;
import java.io.InputStream;
import java.util.ArrayList;
import org.json.*;

/** Página de quadrinhos. Layout compartilhado com o gerador de foco/toques. */
final class MainMenu {
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Path panel = new Path();
    private final RectF dst = new RectF();
    private final ArrayList<Tile> tiles = new ArrayList<>();
    private final Bitmap paper;
    private final Typeface comic;
    private final Context m_Context;
    private static final class Tile {
        String id, title, caption;
        float x, y, w, h;
        Bitmap art;
    }

    MainMenu(Context context) {
        m_Context = context;
        paper = image(context, "character-menu/paper.png");
        comic = Typeface.createFromAsset(context.getAssets(), "character-menu/comic.ttf");
        try (InputStream in = context.getAssets().open("main-menu/layout.json")) {
            java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[4096]; int n;
            while ((n = in.read(buf)) != -1) bytes.write(buf, 0, n);
            JSONArray items = new JSONObject(bytes.toString("UTF-8")).getJSONArray("items");
            for (int i = 0; i < items.length(); i++) {
                JSONObject item = items.getJSONObject(i);
                JSONArray b = item.getJSONArray("bounds");
                Tile t = new Tile();
                t.id = item.getString("id"); t.title = item.getString("title");
                t.caption = item.getString("caption");
                t.x = b.getInt(0); t.y = b.getInt(1); t.w = b.getInt(2); t.h = b.getInt(3);
                if (item.has("art")) t.art = image(context, item.getString("art"));
                tiles.add(t);
            }
        } catch (Exception e) { Log.e("BozPad", "MainMenu: layout inválido", e); }
    }

    private Bitmap image(Context c, String asset) {
        try (InputStream in = c.getAssets().open(asset)) { return BitmapFactory.decodeStream(in); }
        catch (Exception e) { Log.e("BozPad", "MainMenu: " + asset, e); return null; }
    }

    private void text(Canvas c, String s, float x, float y, float size, int color, float maxWidth) {
        p.setTypeface(comic); p.setStyle(Paint.Style.FILL); p.setColor(color);
        p.setTextSize(size);
        if (p.measureText(s) > maxWidth) p.setTextSize(size * maxWidth / p.measureText(s));
        c.drawText(s, x, y, p);
    }

    void draw(Canvas c, int width, int height) {
        c.save(); c.scale(width / 1280f, height / 720f);
        c.drawColor(0xFFF2EEE4);
        p.setColor(Color.WHITE); dst.set(0, 0, 1280, 720);
        if (paper != null) c.drawBitmap(paper, null, dst, p);
        p.setColor(0xFF151211); c.drawRect(48, 134, 1232, 674, p);
        p.setColor(0xFF151211); c.drawRect(64, 38, 1216, 134, p);
        text(c, "BLACK OPS ZOMBIES", 86, 80, 30, 0xFFF2EEE4, 660);
        text(c, "REIMAGINED", 85, 119, 42, 0xFFF2EEE4, 660);
        text(c, "MAIN MENU", 942, 100, 33, 0xFFF2EEE4, 250);
        for (Tile t : tiles) {
            panel.reset(); panel.moveTo(t.x, t.y + 6); panel.lineTo(t.x + t.w, t.y);
            panel.lineTo(t.x + t.w - 3, t.y + t.h - 6); panel.lineTo(t.x + 2, t.y + t.h); panel.close();
            c.save(); c.clipPath(panel);
            p.setColor(0xFF151211); c.drawRect(t.x, t.y, t.x+t.w, t.y+t.h, p);
            if (t.art != null) {
                // Center crop, never stretch the original comic illustrations.
                float artH = t.h - 100;
                float scale = Math.max(t.w / t.art.getWidth(), artH / t.art.getHeight());
                float w = t.art.getWidth()*scale, h = t.art.getHeight()*scale;
                dst.set(t.x+(t.w-w)/2, t.y+(artH-h)/2, t.x+(t.w+w)/2, t.y+(artH+h)/2);
                p.setColor(Color.WHITE); c.drawBitmap(t.art, null, dst, p);
            }
            if (!t.caption.isEmpty()) {
                float top = t.y + t.h - 112;
                p.setColor("single".equals(t.id) ? 0xFF791D24 : 0xFF151211);
                c.drawRect(t.x, top, t.x+t.w, t.y+t.h, p);
                text(c, t.title, t.x+16, top+44, 38, 0xFFF2EEE4, t.w-32);
                text(c, t.caption, t.x+16, top+85, 19, 0xFFD5C9AC, t.w-32);
            } else if ("name".equals(t.id)) {
                text(c, "PLAYER: " + PlayerNames.get(m_Context, 0), t.x+22, t.y+49, 31, 0xFFF2EEE4, t.w-44);
            } else text(c, t.title, t.x+22, t.y+49, 31, 0xFFF2EEE4, t.w-44);
            c.restore();
            p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(5); p.setColor(0xFF151211);
            c.drawPath(panel, p);
            p.setStrokeWidth(1.5f); p.setColor(0xFF827763); c.drawPath(panel, p);
            p.setStyle(Paint.Style.FILL);
        }
        text(c, "D-PAD: NAVIGATE     A: SELECT     B: BACK", 330, 698, 20, 0xFF151211, 780);
        c.restore();
    }
}
