package com.carlos.boz;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Region;
import android.graphics.Typeface;
import android.os.Build;
import android.util.Log;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Telas SWF no estilo das páginas Reimagined (menus-style/screens.json, mesma fonte do menus.py).
 * page: página desenhada inteira; "windows" deixam ver os números dinâmicos do SWF. frame: só a tarja
 * sobre o título original e o rodapé. Os toques continuam indo ao SWF; as sondas leem só a SurfaceView.
 */
final class StyledScreen {
    private static final int PAPER = 0xFFF2EEE4, INK = 0xFF151211, RED = 0xFF791D24, DIM = 0xDE191614;
    private static final String HINT = "D-PAD: NAVIGATE     A: SELECT     B: BACK";

    private static final class Button {
        String id, title, caption;
        RectF bounds;
        boolean primary;
        Bitmap art;
    }

    private static final class Style {
        boolean page;
        String title, hint, back;
        String[] message = {};
        RectF tag, name;
        final ArrayList<RectF> windows = new ArrayList<>();
        final ArrayList<Button> buttons = new ArrayList<>();
    }

    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Path panel = new Path();
    private final RectF dst = new RectF();
    private final Map<String, Style> m_Styles = new HashMap<>();
    private final Map<String, Bitmap> m_Art = new HashMap<>();
    private final Bitmap paper;
    private final Typeface comic;
    private final Context m_Context;

    StyledScreen(Context context) {
        m_Context = context;
        paper = image(context, "character-menu/paper.png");
        Typeface loaded;
        try { loaded = Typeface.createFromAsset(context.getAssets(), "character-menu/comic.ttf"); }
        catch (Exception e) { loaded = Typeface.create("sans-serif-condensed", Typeface.BOLD_ITALIC); }
        comic = loaded;
        try (InputStream in = context.getAssets().open("menus-style/screens.json")) {
            java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[4096]; int n;
            while ((n = in.read(buf)) != -1) bytes.write(buf, 0, n);
            JSONObject screens = new JSONObject(bytes.toString("UTF-8")).getJSONObject("screens");
            for (java.util.Iterator<String> it = screens.keys(); it.hasNext(); ) {
                String id = it.next();
                JSONObject o = screens.getJSONObject(id);
                Style s = new Style();
                s.page = "page".equals(o.getString("kind"));
                s.title = o.getString("title");
                s.hint = o.optString("hint", HINT);
                JSONArray m = o.optJSONArray("message");
                if (m != null) {
                    s.message = new String[m.length()];
                    for (int i = 0; i < m.length(); i++) s.message[i] = m.getString(i);
                }
                if (o.has("tag")) s.tag = rect(o.getJSONArray("tag"));
                if (o.has("name")) s.name = rect(o.getJSONArray("name"));
                s.back = o.optString("back", null);
                JSONArray w = o.optJSONArray("windows");
                for (int i = 0; w != null && i < w.length(); i++) s.windows.add(rect(w.getJSONArray(i)));
                JSONArray b = o.optJSONArray("buttons");
                for (int i = 0; b != null && i < b.length(); i++) {
                    JSONObject bo = b.getJSONObject(i);
                    Button bt = new Button();
                    bt.id = bo.optString("id");
                    bt.title = bo.getString("title");
                    bt.caption = bo.optString("caption", "");
                    bt.bounds = rect(bo.getJSONArray("bounds"));
                    bt.primary = bo.optBoolean("primary");
                    if (bo.has("art")) bt.art = art(context, bo.getString("art"));
                    s.buttons.add(bt);
                }
                m_Styles.put(id, s);
            }
        } catch (Exception e) { Log.e("BozPad", "StyledScreen: screens.json inválido", e); }
    }

    private static RectF rect(JSONArray a) throws Exception {
        return new RectF(a.getInt(0), a.getInt(1), a.getInt(0) + a.getInt(2), a.getInt(1) + a.getInt(3));
    }

    private Bitmap art(Context c, String asset) {
        if (!m_Art.containsKey(asset)) m_Art.put(asset, image(c, asset));
        return m_Art.get(asset);
    }

    private Bitmap image(Context c, String asset) {
        try (InputStream in = c.getAssets().open(asset)) { return BitmapFactory.decodeStream(in); }
        catch (Exception e) { Log.e("BozPad", "StyledScreen: " + asset, e); return null; }
    }

    private Style style(String id) {
        return m_Styles.get("pause".equals(id) && Menus.coopSession() ? "pausecoop" : id);
    }

    boolean handles(String id) { return id != null && m_Styles.containsKey(id); }

    /** Página inteira em Java (cobre o SWF): pode aparecer antes de o SWF chegar nela. */
    boolean isPage(String id) {
        if ("reconnectconfirm".equals(id) && Menus.rejoinDialog()) return false;
        Style s = id != null ? style(id) : null;
        return s != null && s.page;
    }

    /** Tela aberta pelo B nesta tela: id, "*" (muda, destino desconhecido) ou null (B não navega aqui). */
    String back(String id) {
        Style s = id != null ? style(id) : null;
        return s != null ? s.back : null;
    }

    private void text(Canvas c, String s, float x, float y, float size, int color, float maxWidth, boolean outline) {
        p.setTypeface(comic); p.setTextSize(size); p.setStrokeJoin(Paint.Join.ROUND);
        if (p.measureText(s) > maxWidth) p.setTextSize(size * maxWidth / p.measureText(s));
        if (outline) {
            p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(p.getTextSize() * 0.14f);
            p.setColor(INK); c.drawText(s, x, y, p);
        }
        p.setStyle(Paint.Style.FILL); p.setColor(color);
        c.drawText(s, x, y, p);
    }

    private void comicPath(RectF r, float skew) {
        panel.reset();
        panel.moveTo(r.left, r.top + skew); panel.lineTo(r.right, r.top);
        panel.lineTo(r.right - 3, r.bottom - skew); panel.lineTo(r.left + 2, r.bottom); panel.close();
    }

    private void outline(Canvas c) {
        p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(5); p.setColor(INK); c.drawPath(panel, p);
        p.setStrokeWidth(1.5f); p.setColor(0xFF827763); c.drawPath(panel, p);
        p.setStyle(Paint.Style.FILL);
    }

    /** Desenha a tela id; false = sem estilo (o Cursor segue com o desenho padrão). */
    boolean draw(Canvas c, int width, int height, String id) {
        Style s = style(id);
        if (s == null) return false;
        c.save(); c.scale(width / 1280f, height / 720f);
        m_Id = id;
        Kit k = Kit.get(m_Context);
        k.screen = "pause".equals(id) ? "pause_solo" : id;
        if ("reconnectconfirm".equals(id) && Menus.rejoinDialog()) k.hint(c, Kit.HINT);
        else if ("pause".equals(id) && Menus.coopSession()) drawCoopBox(c, k);
        else if (s.page) drawPage(c, s, k);
        else k.hint(c, s.hint);   // a tela do próprio jogo é a referência: só o rodapé comum
        c.restore();
        return true;
    }

    /** Pausa do Co-op: a tela do jogo fica inteira; só esta caixa vai por cima. */
    private void drawCoopBox(Canvas c, Kit k) {
        RectF box = new RectF(448, 26, 858, 138);
        k.plate(c, box, 3, 0xF00D0C0B);
        String map = mapName(Hud.s_Map);
        int round = Hud.s_Round;
        String line = (map != null ? map : "");
        if (map != null && round > 0 && !"deadops".equals(Hud.s_Map)) line += " - ROUND " + round;
        k.text(c, line, 464, 70, 25, Kit.PAPER, 378, true, Paint.Align.LEFT);
        k.panel(c, new RectF(460, 88, 846, 128), null, "TIMER: " + (Hud.s_TimerShown ? "ON" : "OFF"), "", false, false, 0, 0);
    }

    private void drawPage(Canvas c, Style s, Kit k) {
        if (s.message.length > 0) {
            k.burst(c);
            k.plate(c, new RectF(90, 250, 1190, 310 + s.message.length * 56), 6, 0xE80D0C0B);
            for (int i = 0; i < s.message.length; i++)
                k.text(c, s.message[i], 640, 300 + i * 56, 38, Kit.PAPER, 1080, true, Paint.Align.CENTER);
            for (Button b : s.buttons) drawButton(c, b, k);
            k.hint(c, Kit.HINT);
            return;
        }
        c.save();
        for (RectF w : s.windows) {
            if (Build.VERSION.SDK_INT >= 26) c.clipOutRect(w);
            else c.clipRect(w, Region.Op.DIFFERENCE);
        }
        k.sheet(c);
        boolean pause = "pause".equals(m_Id);
        if (pause) {
            String asset = mapBackdrop(Hud.s_Map);
            k.cover(c, asset != null ? art(m_Context, asset) : null,
                    new RectF(Kit.BX0 + 6, Kit.BY0 + 6, Kit.BX1 - 6, Kit.BY1 - 6), 0x8C000000);
        }
        c.restore();
        for (RectF w : s.windows) {
            p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(6); p.setColor(Kit.INK);
            c.drawRect(w.left - 3, w.top - 3, w.right + 3, w.bottom + 3, p);
            p.setStrokeWidth(1.5f); p.setColor(Kit.EDGE);
            c.drawRect(w.left - 1, w.top - 1, w.right + 1, w.bottom + 1, p);
            p.setStyle(Paint.Style.FILL);
        }
        k.pageTitle(c, s.title);
        for (Button b : s.buttons) drawButton(c, b, k);
        if (pause) drawMatch(c, k);
        k.hint(c, s.hint);
    }

    /** Janelas da página id recortadas do canvas (em pixels da View): a cortina por baixo não as cobre. */
    void clipWindows(Canvas c, String id, int width, int height) {
        Style s = style(id);
        if (s == null || !s.page) return;
        for (RectF w : s.windows) {
            RectF r = new RectF(w.left * width / 1280f, w.top * height / 720f,
                    w.right * width / 1280f, w.bottom * height / 720f);
            if (Build.VERSION.SDK_INT >= 26) c.clipOutRect(r);
            else c.clipRect(r, Region.Op.DIFFERENCE);
        }
    }

    private String m_Id;

    /** Cenário do mapa da seleção de mapa (character-menu), atrás do corpo do pause. */
    private static String mapBackdrop(String id) {
        if (id == null) return null;
        switch (id) {
            case "kino": return "character-menu/background.png";
            case "deadops": return "character-menu/arcade-background.png";
            case "ascension": return "character-menu/ascension-background.png";
            case "callofthedead": return "character-menu/cotd-background.png";
            default: return null;
        }
    }

    static String mapName(String id) {
        if (id == null || id.isEmpty()) return null;
        switch (id) {
            case "kino": return "KINO DER TOTEN";
            case "ascension": return "ASCENSION";
            case "callofthedead": return "CALL OF THE DEAD";
            case "deadops": return "DEAD OPS ARCADE";
            case "tutorial": return "TUTORIAL";
            default: return id.toUpperCase(java.util.Locale.ROOT);
        }
    }

    /** Pause: jogador, mapa e round acima da janela do SWF (personagem, kills, headshots e revives). */
    private void drawMatch(Canvas c, Kit k) {
        k.text(c, PlayerNames.get(m_Context, 0), 196, 196, 44, Kit.PAPER, 640, true, Paint.Align.LEFT);
        String map = mapName(Hud.s_Map);
        int round = Hud.s_Round;
        String line = (map != null ? map : "") + (map != null && round > 0 ? "   -   " : "")
                + (round > 0 ? "ROUND " + round : "");
        if (!line.isEmpty()) k.text(c, line, 198, 236, 26, Kit.YELLOW, 640, true, Paint.Align.LEFT);
    }

    /** Cortina entre telas: a folha fica parada; um traço vermelho corre se a espera passar de CURTAIN_HOLD_MS. */
    void drawCurtain(Canvas c, int width, int height, long elapsedMs) {
        c.save(); c.scale(width / 1280f, height / 720f);
        Kit.get(m_Context).sheet(c);
        if (elapsedMs > CURTAIN_HOLD_MS) {
            float in = Math.min(1f, (elapsedMs - CURTAIN_HOLD_MS) / 220f);
            float t = ((elapsedMs - CURTAIN_HOLD_MS) % SWEEP_MS) / (float) SWEEP_MS;
            float e = t * t * (3 - 2 * t);
            float len = 220, x = Kit.BX0 - len + e * (Kit.BX1 - Kit.BX0 + len);
            c.save(); c.clipRect(Kit.BX0, Kit.BY1 - 8, Kit.BX1, Kit.BY1 - 3);
            p.setColor(RED); p.setAlpha(Math.round(255 * in));
            c.drawRect(x, Kit.BY1 - 8, x + len, Kit.BY1 - 3, p);
            p.setAlpha(255);
            c.restore();
        }
        c.restore();
    }

    static final long CURTAIN_HOLD_MS = 250, SWEEP_MS = 1100;

    private void drawButton(Canvas c, Button b, Kit k) {
        RectF r = b.bounds;
        switch (b.id) {
            case "back": case "no": case "quit": case "return":
                k.arrow(c, true, r, b.title, b.id, false); return;
            case "ok": case "yes": case "retry":
                k.arrow(c, false, r, b.title, b.id, false); return;
            default:
        }
        String title = "timer".equals(b.id) ? "TIMER: " + (Hud.s_TimerShown ? "ON" : "OFF")
                : "fps".equals(b.id) ? "FPS: " + (Hud.s_FpsShown ? "ON" : "OFF") : b.title;
        k.panel(c, r, b.art, title, b.caption, false, false, 0, 0);
    }
}
