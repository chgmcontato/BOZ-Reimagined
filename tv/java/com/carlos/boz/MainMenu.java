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
        Kit k = Kit.get(m_Context);
        k.screen = "main";
        c.save(); c.scale(width / 1280f, height / 720f);
        k.sheet(c);
        k.pageTitle(c, "MAIN MENU");
        for (Tile t : tiles) {
            RectF r = new RectF(t.x, t.y, t.x + t.w, t.y + t.h);
            if ("quit".equals(t.id)) k.arrow(c, true, r, t.title, "quit", false);
            else {
                String title = "name".equals(t.id) ? "PLAYER: " + PlayerNames.get(m_Context, 0) : t.title;
                boolean terminal = "profile".equals(t.id);   // main.psd: o terminal vai até o topo do painel
                k.panel(c, r, t.art, title, t.caption, false, false, terminal ? 357 : 0, terminal ? 168 : 0);
            }
        }
        k.hint(c, Kit.HINT);
        c.restore();
    }
}
