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

    boolean handles(String id) { return id != null && m_Styles.containsKey(id); }

    /** Página inteira em Java (cobre o SWF): pode aparecer antes de o SWF chegar nela. */
    boolean isPage(String id) {
        Style s = id != null ? m_Styles.get(id) : null;
        return s != null && s.page;
    }

    /** Tela aberta pelo B nesta tela: id, "*" (muda, destino desconhecido) ou null (B não navega aqui). */
    String back(String id) {
        Style s = id != null ? m_Styles.get(id) : null;
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
        Style s = m_Styles.get(id);
        if (s == null) return false;
        c.save(); c.scale(width / 1280f, height / 720f);
        p.setTextAlign(Paint.Align.LEFT);
        m_Id = id;
        if (s.page) drawPage(c, s); else drawFrame(c, s);
        if ("pause".equals(id)) drawMatch(c);
        p.setTextAlign(Paint.Align.CENTER);
        // Rodapé com fundo de tinta: em frame fica sobre a borda de papel do SWF.
        if (!s.page) { p.setColor(INK); c.drawRect(380, 684, 900, 714, p); }
        text(c, s.hint, 640, s.page ? 698 : 706, s.page ? 20 : 17, s.page ? INK : PAPER, 500 + (s.page ? 280 : 0), false);
        p.setTextAlign(Paint.Align.LEFT);
        c.restore();
        return true;
    }

    private void drawPage(Canvas c, Style s) {
        c.save();
        for (RectF w : s.windows) {
            if (Build.VERSION.SDK_INT >= 26) c.clipOutRect(w);
            else c.clipRect(w, Region.Op.DIFFERENCE);
        }
        drawChrome(c);
        if ("pause".equals(m_Id)) drawMapBackdrop(c);
        c.restore();
        p.setTextAlign(Paint.Align.RIGHT);
        text(c, s.title, 1194, 100, 33, PAPER, 420, false);
        p.setTextAlign(Paint.Align.LEFT);
        for (RectF w : s.windows) {
            p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(6); p.setColor(INK);
            c.drawRect(w, p);
            p.setStrokeWidth(1.5f); p.setColor(0xFF827763); c.drawRect(w, p);
            p.setStyle(Paint.Style.FILL);
        }
        p.setTextAlign(Paint.Align.CENTER);
        for (int i = 0; i < s.message.length; i++)
            text(c, s.message[i], 640, 260 + i * 56, 40, PAPER, 1080, true);
        p.setTextAlign(Paint.Align.LEFT);
        for (Button b : s.buttons) drawButton(c, b);
    }

    /** Janelas da página id recortadas do canvas (em pixels da View): a cortina por baixo não as cobre. */
    void clipWindows(Canvas c, String id, int width, int height) {
        Style s = m_Styles.get(id);
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

    /**
     * Um painel só (48,38-1232,674) com o cenário: cobre a faixa do título e o corpo, que antes ficavam em blocos
     * pretos de larguras diferentes. Escurece para o texto, mais forte no topo, e redesenha o título por cima.
     */
    private void drawMapBackdrop(Canvas c) {
        String asset = mapBackdrop(Hud.s_Map);
        Bitmap b = asset != null ? art(m_Context, asset) : null;
        if (b == null) return;
        RectF body = new RectF(48, 38, 1232, 674);
        float h = b.getWidth() * body.height() / body.width();
        float top = (b.getHeight() - h) * 0.4f;
        android.graphics.Rect src = new android.graphics.Rect(0, Math.round(top), b.getWidth(), Math.round(top + h));
        p.setColor(0xFFFFFFFF);
        c.drawBitmap(b, src, body, p);
        p.setColor(0x8C151211);   // INK translúcido
        c.drawRect(body, p);
        p.setShader(new android.graphics.LinearGradient(0, 38, 0, 170, 0xB0151211, 0x00151211,
                android.graphics.Shader.TileMode.CLAMP));
        c.drawRect(48, 38, 1232, 170, p);
        p.setShader(null);
        p.setTextAlign(Paint.Align.LEFT);
        text(c, "BLACK OPS ZOMBIES", 86, 80, 30, PAPER, 660, false);
        text(c, "REIMAGINED", 85, 119, 42, PAPER, 660, false);
    }

    private static String mapName(String id) {
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
    private void drawMatch(Canvas c) {
        text(c, PlayerNames.get(m_Context, 0), 196, 214, 44, PAPER, 640, false);
        String map = mapName(Hud.s_Map);
        int round = Hud.s_Round;
        String line = (map != null ? map : "") + (map != null && round > 0 ? "   \u2022   " : "")
                + (round > 0 ? "ROUND " + round : "");
        if (!line.isEmpty()) text(c, line, 198, 252, 26, 0xFFD5C9AC, 640, false);
    }

    /** Papel, faixa do título e corpo de tinta: iguais em todas as páginas (e no MainMenu). */
    private void drawChrome(Canvas c) {
        c.drawColor(PAPER);
        p.setColor(0xFFFFFFFF); dst.set(0, 0, 1280, 720);
        if (paper != null) c.drawBitmap(paper, null, dst, p);
        p.setColor(INK); c.drawRect(48, 134, 1232, 674, p);
        c.drawRect(64, 38, 1216, 134, p);
        p.setTextAlign(Paint.Align.LEFT);
        text(c, "BLACK OPS ZOMBIES", 86, 80, 30, PAPER, 660, false);
        text(c, "REIMAGINED", 85, 119, 42, PAPER, 660, false);
    }

    /**
     * Cortina entre telas: a moldura fica parada e só o conteúdo troca. Se a espera passar de CURTAIN_HOLD_MS, um
     * traço vermelho fino corre sob a faixa do título (sinal de que o jogo está trocando de tela).
     */
    void drawCurtain(Canvas c, int width, int height, long elapsedMs) {
        c.save(); c.scale(width / 1280f, height / 720f);
        drawChrome(c);
        if (elapsedMs > CURTAIN_HOLD_MS) {
            float in = Math.min(1f, (elapsedMs - CURTAIN_HOLD_MS) / 220f);
            float t = ((elapsedMs - CURTAIN_HOLD_MS) % SWEEP_MS) / (float) SWEEP_MS;
            float e = t * t * (3 - 2 * t);                   // ease in-out
            float len = 220, x = 64 - len + e * (1152 + len);
            c.save(); c.clipRect(64, 128, 1216, 134);
            p.setColor(RED); p.setAlpha(Math.round(255 * in));
            c.drawRect(x, 129, x + len, 134, p);
            p.setAlpha(255);
            c.restore();
        }
        c.restore();
    }

    static final long CURTAIN_HOLD_MS = 250, SWEEP_MS = 1100;

    private void drawButton(Canvas c, Button b) {
        RectF r = b.bounds;
        comicPath(r, r.height() > 100 ? 6 : 4);
        c.save(); c.clipPath(panel);
        p.setColor(b.primary && b.art == null ? RED : DIM); c.drawRect(r, p);
        if (b.art != null) {
            // Center crop, never stretch the original comic illustrations.
            float artH = r.height() - 100;
            float scale = Math.max(r.width() / b.art.getWidth(), artH / b.art.getHeight());
            float w = b.art.getWidth() * scale, h = b.art.getHeight() * scale;
            dst.set(r.centerX() - w / 2, r.top + (artH - h) / 2, r.centerX() + w / 2, r.top + (artH + h) / 2);
            p.setColor(0xFFFFFFFF); c.drawBitmap(b.art, null, dst, p);
        }
        if (!b.caption.isEmpty()) {
            float top = r.bottom - 112;
            p.setColor(b.primary ? RED : INK); c.drawRect(r.left, top, r.right, r.bottom, p);
            text(c, b.title, r.left + 16, top + 44, 38, PAPER, r.width() - 32, false);
            text(c, b.caption, r.left + 16, top + 85, 19, 0xFFD5C9AC, r.width() - 32, false);
        } else {
            float size = Math.min(31, r.height() * 0.42f);
            String title = "timer".equals(b.id) ? "TIMER: " + (Hud.s_TimerShown ? "ON" : "OFF") : b.title;
            text(c, title, r.left + 22, r.centerY() + size * 0.36f, size, PAPER, r.width() - 44, false);
        }
        c.restore();
        outline(c);
    }

    private void drawFrame(Canvas c, Style s) {
        if (s.tag == null) return;
        comicPath(s.tag, 5);
        p.setColor(INK); c.drawPath(panel, p);
        outline(c);
        float size = Math.min(44, s.tag.height() * 0.62f);
        text(c, s.title, s.tag.left + 18, s.tag.centerY() + size * 0.36f, size, PAPER, s.tag.width() - 36, false);
        if (s.name != null) {
            // Placa opcional ("name" no screens.json) com o nome do P1 sobre um rótulo do SWF.
            comicPath(s.name, 4);
            p.setColor(INK); c.drawPath(panel, p);
            outline(c);
            float ns = Math.min(30, s.name.height() * 0.62f), y = s.name.centerY() + ns * 0.36f;
            text(c, "NAME:", s.name.left + 16, y, ns, 0xFFD5C9AC, 120, false);
            p.setTypeface(comic); p.setTextSize(ns);
            float x = s.name.left + 24 + p.measureText("NAME:");
            text(c, PlayerNames.get(m_Context, 0), x, y, ns, PAPER, s.name.right - 16 - x, false);
        }
    }
}
