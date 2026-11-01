package com.carlos.boz;

import android.app.Activity;
import android.graphics.Bitmap;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.util.Log;
import android.view.PixelCopy;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * Navegação dos menus por botões, como num console.
 * Reconhece a tela atual comparando uma "sonda" (área fixa, em geral o título) com referências tiradas dos
 * prints (menus/menus.py -> assets/boz_menus.json) e move um foco entre os botões medidos daquela tela.
 * Coordenadas do mapa em 1280x720; a imagem do jogo é lida com PixelCopy numa grade de 160x90 em cinza.
 */
final class Menus {
    private static final String TAG = "BozPad";
    private static final float THRESHOLD = 16f;   // mesma tela: ~8; telas diferentes: > 26
    private static final float THRESHOLD2 = 26f;  // 2ª sonda (sobre o cenário 3D, varia mais)
    private static final long INTERVAL_MS = 120;      // modo menu (a transição do Cursor espera esta leitura)
    private static final long IDLE_INTERVAL_MS = 500; // modo jogo: só para notar pausa/Game Over

    static final int LEFT = 0, RIGHT = 1, UP = 2, DOWN = 3;

    /** Item da tela: botão (retângulo) ou slider (trilho de a até b). */
    static final class Item {
        final boolean slider;
        final boolean toGame;        // tocar neste botão volta à partida (ex.: RETURN da pausa)
        final int map;               // "map:N": escolhe o mapa N da lista do menu (Cursor opera o carrossel)
        final int character;
        final boolean start;
        final boolean selection;
        final boolean nameEditor;    // "name": abre o editor de nome (tela só do Android, sem toque no SWF)
        final boolean timerToggle;   // "timer": liga/desliga o timer da partida na tela (só Android)
        final String to;             // "to:ID": tela que o toque abre ("*" = muda, destino desconhecido); null = não sabe
        final boolean stay;          // "stay": o toque não muda de tela (caixinhas, botões internos)
        final float tx, ty;          // "tap:X,Y": A toca nesse ponto em vez do centro (ex.: caixinha ao lado do rótulo)
        final float x, y, w, h;      // botão
        final float ax, ay, bx, by;  // slider
        Item(JSONArray a) throws Exception {
            slider = "s".equals(a.getString(0));
            // Do índice 5 em diante: ação e/ou "tap:X,Y", em qualquer ordem.
            String action = "", tap = null, dest = null;
            boolean st = false;
            for (int i = 5; i < a.length(); i++) {
                String t = a.optString(i);
                if (t.startsWith("tap:")) tap = t.substring(4);
                else if (t.startsWith("to:")) dest = t.substring(3);
                else if (t.equals("stay")) st = true;
                else action = t;
            }
            to = dest;
            stay = st;
            toGame = "game".equals(action);
            map = action.startsWith("map:") ? Integer.parseInt(action.substring(4)) : -1;
            character = action.startsWith("char:") ? Integer.parseInt(action.substring(5)) : -1;
            start = "start".equals(action);
            selection = "selection".equals(action);
            nameEditor = "name".equals(action);
            timerToggle = "timer".equals(action);
            if (slider) {
                ax = a.getInt(1); ay = a.getInt(2); bx = a.getInt(3); by = a.getInt(4);
                x = Math.min(ax, bx) - 20; y = Math.min(ay, by) - 20;
                w = Math.abs(bx - ax) + 40; h = Math.abs(by - ay) + 40;
            } else {
                x = a.getInt(1); y = a.getInt(2); w = a.getInt(3); h = a.getInt(4);
                ax = ay = bx = by = 0;
            }
            if (tap != null) {
                String[] p = tap.split(",");
                tx = Float.parseFloat(p[0]); ty = Float.parseFloat(p[1]);
            } else {
                tx = cx(); ty = cy();
            }
        }
        float cx() { return x + w / 2; }
        float cy() { return y + h / 2; }
        boolean horizontal() { return Math.abs(bx - ax) >= Math.abs(by - ay); }
    }

    private static final class Screen {
        String id;
        int px, py, pw, ph;
        int[] ref;
        int[] probe2;     // opcional: {x, y, w, h} em células; a tela só casa se as duas sondas casarem
        int[] ref2;
        java.util.Set<String> after;   // opcional: só vale vindo destas telas
        final List<Item> items = new ArrayList<>();
        int focus;
    }

    private final Activity m_Activity;
    private final Runnable m_OnChange;
    private boolean m_Focus;            // modo menu: foco visível e entrada ativa
    private final List<Screen> m_Screens = new ArrayList<>();
    private int m_Cell = 8, m_Shift = 2, m_GW = 160, m_GH = 90;
    private final Handler m_Main = new Handler(Looper.getMainLooper());
    private final Handler m_Copy;
    private Bitmap m_Bitmap;
    private SurfaceView m_Surface;
    private volatile int[] m_Px;         // última imagem em cores (2x a grade: 320x180), para achar o botão dos sliders
    private Screen m_Current;
    private Screen m_LastKnown;          // última tela reconhecida antes da atual (para "after")
    private boolean m_Running;
    private boolean m_Busy;
    private long m_UnknownSince;

    Menus(Activity activity, Runnable onChange) {
        m_Activity = activity;
        m_OnChange = onChange;
        HandlerThread t = new HandlerThread("BozMenus");
        t.start();
        m_Copy = new Handler(t.getLooper());
        load();
    }

    private void load() {
        try (InputStream in = m_Activity.getAssets().open("boz_menus.json")) {
            byte[] buf = new byte[in.available()];
            int n = 0;
            while (n < buf.length) n += in.read(buf, n, buf.length - n);
            JSONObject root = new JSONObject(new String(buf, "UTF-8"));
            m_Cell = root.getInt("cell");
            m_Shift = root.optInt("shift", 2);
            m_GW = root.getInt("w") / m_Cell;
            m_GH = root.getInt("h") / m_Cell;
            JSONArray screens = root.getJSONArray("screens");
            for (int i = 0; i < screens.length(); i++) {
                JSONObject o = screens.getJSONObject(i);
                Screen s = new Screen();
                s.id = o.getString("id");
                JSONArray p = o.getJSONArray("probe");
                s.px = p.getInt(0); s.py = p.getInt(1); s.pw = p.getInt(2); s.ph = p.getInt(3);
                JSONArray r = o.getJSONArray("ref");
                s.ref = new int[r.length()];
                for (int k = 0; k < s.ref.length; k++) s.ref[k] = r.getInt(k);
                if (o.has("probe2")) {
                    JSONArray p2 = o.getJSONArray("probe2");
                    s.probe2 = new int[]{p2.getInt(0), p2.getInt(1), p2.getInt(2), p2.getInt(3)};
                    JSONArray r2 = o.getJSONArray("ref2");
                    s.ref2 = new int[r2.length()];
                    for (int k = 0; k < s.ref2.length; k++) s.ref2[k] = r2.getInt(k);
                }
                JSONArray af = o.optJSONArray("after");
                if (af != null) {
                    s.after = new java.util.HashSet<>();
                    for (int k = 0; k < af.length(); k++) s.after.add(af.getString(k));
                }
                JSONArray items = o.getJSONArray("items");
                for (int k = 0; k < items.length(); k++) s.items.add(new Item(items.getJSONArray(k)));
                m_Screens.add(s);
            }
            Log.i(TAG, "Menus: " + m_Screens.size() + " telas mapeadas");
        } catch (Exception e) {
            Log.e(TAG, "Menus: sem mapa", e);
        }
    }

    /** Reconhecimento roda sempre; no modo jogo mais devagar e sem foco. */
    void setFocusMode(boolean focus) {
        m_Focus = focus;
        if (!m_Running) {
            m_Running = true;
            m_Main.post(m_Tick);
        }
        m_OnChange.run();
    }

    /** Modo menu com tela reconhecida (null = desconhecida: usa a seta). */
    boolean active() {
        return m_Focus && m_Current != null;
    }

    String screenId() {
        return m_Current != null ? m_Current.id : null;
    }

    Item focused() {
        Screen s = m_Focus ? m_Current : null;
        return s != null && !s.items.isEmpty() ? s.items.get(s.focus) : null;
    }

    private final Runnable m_Tick = new Runnable() {
        @Override
        public void run() {
            if (!m_Running) return;
            capture();
            m_Main.postDelayed(this, m_Focus ? INTERVAL_MS : IDLE_INTERVAL_MS);
        }
    };

    private SurfaceView findSurface(View v) {
        if (v instanceof SurfaceView) return (SurfaceView) v;
        if (v instanceof ViewGroup) {
            ViewGroup g = (ViewGroup) v;
            for (int i = 0; i < g.getChildCount(); i++) {
                SurfaceView s = findSurface(g.getChildAt(i));
                if (s != null) return s;
            }
        }
        return null;
    }

    private void capture() {
        if (m_Busy || m_Screens.isEmpty()) return;
        if (m_Surface == null || !m_Surface.isAttachedToWindow())
            m_Surface = findSurface(m_Activity.getWindow().getDecorView());
        if (m_Surface == null || m_Surface.getWidth() == 0) return;
        if (m_Bitmap == null) m_Bitmap = Bitmap.createBitmap(m_GW * 2, m_GH * 2, Bitmap.Config.ARGB_8888);
        m_Busy = true;
        try {
            PixelCopy.request(m_Surface, m_Bitmap, result -> {
                m_Busy = false;
                if (result != PixelCopy.SUCCESS) return;
                int[] px = new int[m_Bitmap.getWidth() * m_Bitmap.getHeight()];
                m_Bitmap.getPixels(px, 0, m_Bitmap.getWidth(), 0, 0, m_Bitmap.getWidth(), m_Bitmap.getHeight());
                m_Px = px;
                int[] gray = toGray(m_Bitmap);
                Screen s = recognize(blur(gray));
                m_Main.post(() -> setCurrent(s));
            }, m_Copy);
        } catch (Exception e) {
            m_Busy = false;
        }
    }

    /** Bitmap 2x maior que a grade -> média 2x2 em cinza (0.299R + 0.587G + 0.114B, como no menus.py). */
    private int[] toGray(Bitmap b) {
        int bw = b.getWidth();
        int[] px = new int[bw * b.getHeight()];
        b.getPixels(px, 0, bw, 0, 0, bw, b.getHeight());
        int[] g = new int[m_GW * m_GH];
        for (int y = 0; y < m_GH; y++) {
            for (int x = 0; x < m_GW; x++) {
                int acc = 0;
                for (int k = 0; k < 4; k++) {
                    int c = px[(y * 2 + (k >> 1)) * bw + x * 2 + (k & 1)];
                    acc += (299 * ((c >> 16) & 255) + 587 * ((c >> 8) & 255) + 114 * (c & 255)) / 1000;
                }
                g[y * m_GW + x] = acc / 4;
            }
        }
        return g;
    }

    private int[] blur(int[] g) {
        int[] out = new int[g.length];
        for (int y = 0; y < m_GH; y++) {
            for (int x = 0; x < m_GW; x++) {
                int acc = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    int yy = Math.min(m_GH - 1, Math.max(0, y + dy));
                    for (int dx = -1; dx <= 1; dx++) {
                        int xx = Math.min(m_GW - 1, Math.max(0, x + dx));
                        acc += g[yy * m_GW + xx];
                    }
                }
                out[y * m_GW + x] = acc / 9;
            }
        }
        return out;
    }

    private Screen recognize(int[] g) {
        Screen best = null;
        float bestD = THRESHOLD;
        for (Screen s : m_Screens) {
            if (s.after != null && s != m_Current && (m_LastKnown == null || !s.after.contains(m_LastKnown.id))) continue;
            float d = match(s, g);
            if (d < bestD) { bestD = d; best = s; }
        }
        return best;
    }

    /** A sonda principal decide; a 2ª (mais folgada) desempata telas de mesmo título (como no menus.py). */
    private float match(Screen s, int[] g) {
        float d = match(s.px, s.py, s.pw, s.ph, s.ref, g);
        if (s.probe2 != null && d < THRESHOLD) {
            float d2 = match(s.probe2[0], s.probe2[1], s.probe2[2], s.probe2[3], s.ref2, g);
            d = d2 >= THRESHOLD2 ? Float.MAX_VALUE : (d + d2) / 2;
        }
        return d;
    }

    /** Menor distância média de uma sonda, testando deslocamentos de até ±shift células. */
    private float match(int px, int py, int pw, int ph, int[] ref, int[] g) {
        float best = Float.MAX_VALUE;
        for (int dy = -m_Shift; dy <= m_Shift; dy++) {
            for (int dx = -m_Shift; dx <= m_Shift; dx++) {
                int cx = px + dx, cy = py + dy;
                if (cx < 0 || cy < 0 || cx + pw > m_GW || cy + ph > m_GH) continue;
                long sum = 0;
                int k = 0;
                for (int j = 0; j < ph; j++)
                    for (int i = 0; i < pw; i++)
                        sum += Math.abs(g[(cy + j) * m_GW + cx + i] - ref[k++]);
                best = Math.min(best, sum / (float) ref.length);
            }
        }
        return best;
    }

    private void setCurrent(Screen s) {
        // O carrossel pode animar sobre a sonda. Evita piscar o menu e perder o foco.
        if (s == null && m_Current != null && "charselect".equals(m_Current.id)) {
            long now = android.os.SystemClock.uptimeMillis();
            if (m_UnknownSince == 0) m_UnknownSince = now;
            if (now - m_UnknownSince < 750) return;
        } else m_UnknownSince = 0;
        if (s == m_Current) return;
        if (m_Current != null) m_LastKnown = m_Current;
        m_Current = s;
        if (s != null) s.focus = 0;
        Log.i(TAG, "Menus: tela " + (s != null ? s.id : "desconhecida"));
        m_OnChange.run();
    }

    // ---------------------------------------------------------------- foco

    int focusIndex() { return m_Current != null ? m_Current.focus : -1; }

    /** Move o foco na direção; num slider, ←/→ (ou ↑/↓ se vertical) ajusta o valor. Retorna o slider a arrastar. */
    float[] move(int dir) {
        return move(dir, false);
    }

    /** edit = slider "agarrado" (A): a direção ao longo do trilho ajusta em vez de navegar. */
    float[] move(int dir, boolean edit) {
        Screen s = m_Current;
        if (s == null || s.items.isEmpty()) return null;
        Item cur = s.items.get(s.focus);
        if (edit && cur.slider && cur.horizontal() == (dir == LEFT || dir == RIGHT)) {
            // sinal = direção apertada (na tela) projetada no trilho a->b
            float ddx = dir == LEFT ? -1 : dir == RIGHT ? 1 : 0, ddy = dir == UP ? -1 : dir == DOWN ? 1 : 0;
            float dot = ddx * (cur.bx - cur.ax) + ddy * (cur.by - cur.ay);
            return sliderStep(cur, dot >= 0 ? 1 : -1);
        }
        int best = -1;
        float bestScore = Float.MAX_VALUE;
        for (int i = 0; i < s.items.size(); i++) {
            if (i == s.focus) continue;
            Item it = s.items.get(i);
            float dx = it.cx() - cur.cx(), dy = it.cy() - cur.cy();
            float along, across;
            switch (dir) {
                case LEFT: along = -dx; across = dy; break;
                case RIGHT: along = dx; across = dy; break;
                case UP: along = -dy; across = dx; break;
                default: along = dy; across = dx; break;
            }
            if (along <= 10) continue;
            float score = along + 2.5f * Math.abs(across);
            if (score < bestScore) { bestScore = score; best = i; }
        }
        if (best >= 0) s.focus = best;
        m_OnChange.run();
        return null;
    }

    /**
     * Assinatura da arte grande do mapa (centro da tela de personagem, x 450..840, y 60..390 em 1280x720): cinza
     * médio numa grade 8x6, da última imagem. null antes da primeira captura.
     */
    int[] artSignature() {
        int[] px = m_Px;
        if (px == null) return null;
        int bw = m_GW * 2;
        float scale = bw / 1280f;
        int[] sig = new int[48];
        for (int gy = 0; gy < 6; gy++)
            for (int gx = 0; gx < 8; gx++) {
                int x0 = Math.round((450 + gx * 48.75f) * scale), y0 = Math.round((60 + gy * 55f) * scale);
                int x1 = Math.round((450 + (gx + 1) * 48.75f) * scale), y1 = Math.round((60 + (gy + 1) * 55f) * scale);
                int sum = 0, n = 0;
                for (int y = y0; y < y1; y++)
                    for (int x = x0; x < x1; x++) {
                        int c = px[y * bw + x];
                        sum += (((c >> 16) & 255) * 299 + ((c >> 8) & 255) * 587 + (c & 255) * 114) / 1000;
                        n++;
                    }
                sig[gy * 8 + gx] = n > 0 ? sum / n : 0;
            }
        return sig;
    }

    /** Diferença média entre duas assinaturas (0..255). */
    static float artDiff(int[] a, int[] b) {
        if (a == null || b == null) return 0;
        float d = 0;
        for (int i = 0; i < a.length; i++) d += Math.abs(a[i] - b[i]);
        return d / a.length;
    }

    /**
     * Acha o botão do slider na última imagem (320x180, em cores): ao longo do trilho, o trecho cuja cor mais
     * destoa da cor típica do trilho (botões laranja, brancos ou pretos). Retorna {x1, y1, x2, y2} (1280x720)
     * do botão até o fim do trilho na direção pedida (o Cursor desliza enquanto a direção estiver segura).
     */
    private float[] sliderStep(Item it, int sign) {
        int[] px = m_Px;
        if (px == null) return null;
        int bw = m_GW * 2, bh = m_GH * 2;
        float scale = bw / 1280f;
        int n = Math.max(8, Math.round((float) Math.hypot(it.bx - it.ax, it.by - it.ay) * scale));
        int[] r = new int[n], g = new int[n], b = new int[n];
        for (int i = 0; i < n; i++) {
            float t = i / (float) (n - 1);
            int x = Math.round((it.ax + (it.bx - it.ax) * t) * scale);
            int y = Math.round((it.ay + (it.by - it.ay) * t) * scale);
            // média de uma faixa de 3 px atravessando o trilho
            int sr = 0, sg = 0, sb = 0;
            for (int k = -1; k <= 1; k++) {
                int xx = it.horizontal() ? x : x + k, yy = it.horizontal() ? y + k : y;
                int c = px[Math.min(bh - 1, Math.max(0, yy)) * bw + Math.min(bw - 1, Math.max(0, xx))];
                sr += (c >> 16) & 255; sg += (c >> 8) & 255; sb += c & 255;
            }
            r[i] = sr / 3; g[i] = sg / 3; b[i] = sb / 3;
        }
        int mr = median(r), mg = median(g), mb = median(b);
        int win = Math.max(2, n / 12), bestI = 0;
        long bestDev = -1;
        for (int i = 0; i + win <= n; i++) {
            long dev = 0;
            for (int k = 0; k < win; k++)
                dev += Math.abs(r[i + k] - mr) + Math.abs(g[i + k] - mg) + Math.abs(b[i + k] - mb);
            if (dev > bestDev) { bestDev = dev; bestI = i; }
        }
        float t = (bestI + (win - 1) / 2f) / (n - 1);
        float t2 = sign > 0 ? 1f : 0f;
        Log.i(TAG, "slider: botão em " + Math.round(t * 100) + "%");
        return new float[]{
                it.ax + (it.bx - it.ax) * t, it.ay + (it.by - it.ay) * t,
                it.ax + (it.bx - it.ax) * t2, it.ay + (it.by - it.ay) * t2};
    }

    private static int median(int[] v) {
        int[] s = v.clone();
        java.util.Arrays.sort(s);
        return s[s.length / 2];
    }

}
