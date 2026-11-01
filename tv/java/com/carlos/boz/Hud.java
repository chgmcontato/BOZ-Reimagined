package com.carlos.boz;

import android.app.Activity;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import java.io.BufferedReader;
import java.io.FileReader;

/**
 * Indicadores de granada (frag no RB, Monkey Bomb no LB, claymore no direcional direito), um para cada item.
 * O HUD do jogo tem um indicador só, que alterna entre o slot letal e o tático (e o tático agora alterna
 * entre a Monkey Bomb e a claymore guardada no banco do patch_s3e.py); ele fica escondido e esta camada
 * lê o inventário direto da memória do jogo (libcore.io.Memory).
 */
final class Hud {
    private static final String TAG = "BozHud";

    // boz.s3e: assinatura usada para achar a imagem do jogo (a mesma do frida/run.py)
    private static final byte[] SIG = {(byte) 0x2d, (byte) 0xe9, (byte) 0xf0, 0x43, 0x1c, 0x46, 0x66, 0x4b,
            (byte) 0xc1, (byte) 0xb0, 0x07, 0x46, (byte) 0x89, 0x46, 0x7b, 0x44};
    private static final long SIG_OFFSET = 0x1d4ae0;
    private static final long TIMER = 0x410700;    // snapshot nativo, patch_s3e.py/timer.s
    private static final int TIMER_MAGIC = 0x54494d31;
    private static final long BANK = 0x4108f0;      // item, quantidade, jogador (patch_s3e.py, CAVE)
    private static final long WAVES = 0x4958c4;     // g_CWaveManager: +0x78 = round atual
    private static final long LEVEL = 0x4958d0;     // g_CLevelManager: +0x55 = nome interno do mapa
    // jogador: letal [+0x1ec] / quantidade [+0x1cc]; tático [+0x1f0] / [+0x1d0]; item: hash do nome em [+0x5c]
    private static final int FRAG = 0x595f545a, MONKEY = 0x1561c758, CLAYMORE = 0x3dc9e501;
    // djb2 dos nomes das armas: bh_bomb (Gersh, Ascension) e matryoshka_doll (Ascension e Call of the Dead)
    private static final int GERSH = 0x75121c4e, MATRYOSHKA = 0x832c1d12;

    /** Posição dos indicadores, em pixels da tela do jogo (1280x720): coluna à direita, onde ficava o do jogo. */
    private static final float HUD_X = 1166, HUD_Y = 280, ROW = 54, ICON = 44, DIGIT_H = 40;
    // Abaixo da arma/munição (y=0..110), acima das granadas (y=280).
    private static final float TIMER_X = 1232, TIMER_Y = 150;   // TIMER_Y = topo dos dígitos
    private static final long POLL_MS = 250;

    private final Cursor m_Cursor;
    private final HudView m_View;
    private final Handler m_Main;
    private final Handler m_Worker;
    private static java.lang.reflect.Method s_PeekInt, s_PeekBytes, s_PokeInt;
    private java.util.List<long[]> m_Maps = new java.util.ArrayList<>();
    private long m_MapsAt;
    private long m_Base = -1;
    private long m_NextSearch;      // a busca pela imagem lê alguns MB: no máximo a cada 2 s
    // frag, monkey, claymore, gersh, matryoshka (-1 = não tem); um tático por vez, como no jogo original
    private static final int ITEMS = 5;
    private volatile int[] m_Counts = {-1, -1, -1, -1, -1};
    private volatile TimerSnapshot m_Timer;
    // dados da partida para a tela de pause (StyledScreen); lidos só com o pause aberto
    static volatile int s_Round = -1;
    static volatile String s_Map;
    static volatile boolean s_DeadOps;
    // timer na tela (botão TIMER do pause); só esconde, a contagem continua
    static volatile boolean s_TimerShown = true;

    static void toggleTimer() {
        Hud h = s_Self;
        s_TimerShown = !s_TimerShown;
        if (h != null) {
            h.m_Prefs.edit().putBoolean("timerShown", s_TimerShown).apply();
            h.m_Main.post(() -> { h.m_View.m_ShowTimer = h.m_View.m_ShowTimer && s_TimerShown; h.m_View.invalidate(); });
        }
    }
    private final java.util.concurrent.atomic.AtomicInteger m_TimerFence = new java.util.concurrent.atomic.AtomicInteger();
    private int m_LastSaveStatus;
    private boolean m_Leaving;      // Save & Quit / Quit escolhido: esconde até a próxima partida

    // Save & Quit -> Continue: o tempo da partida fica nas preferências (o .i3d não pode ser alterado de fora) e
    // volta ao nativo pelo campo pendente do timer (state+52/56, flag +60, timer.s)
    private static final String SAVED_MS = "matchSavedMs", SAVED_MAP = "matchSavedMap";
    private String m_LastMap;       // último nome de mapa lido na partida (vai junto no Save & Quit)
    private String m_ContinueMap;   // mapa do save, para quando a partida do Continue não informa o nome
    private String m_MatchLog;
    private static Hud s_Self;
    private final android.content.SharedPreferences m_Prefs;
    private long m_Restore = -1;
    private long m_LastElapsed;     // último tempo da partida com o timer ativo (poll)
    private long m_RestoreUntil;

    /** YES do Save & Quit: guarda o tempo jogado da partida. */
    static void onSaveQuit() {
        Hud h = s_Self;
        // no YES o jogo já pode ter encerrado o timer: vale o último valor lido com a partida ativa
        if (h != null) h.m_Worker.post(() -> {
            h.m_Prefs.edit().putLong(SAVED_MS, h.m_LastElapsed).putString(SAVED_MAP, h.m_LastMap).apply();
            Log.i(TAG, "tempo da partida salvo: " + h.m_LastElapsed + " ms");
        });
    }

    /** CONTINUE: a próxima partida que começar recebe o tempo salvo. */
    static void onContinue() {
        Hud h = s_Self;
        if (h != null) h.m_Worker.post(() -> {
            h.m_Restore = h.m_Prefs.getLong(SAVED_MS, 0);
            h.m_ContinueMap = h.m_Prefs.getString(SAVED_MAP, null);
            h.m_RestoreUntil = android.os.SystemClock.uptimeMillis() + 120000;
        });
    }

    private void restoreTimer(TimerSnapshot timer) {
        if (m_Restore < 0)
            return;
        if (android.os.SystemClock.uptimeMillis() > m_RestoreUntil) {
            m_Restore = -1;
            return;
        }
        if (timer == null || !timer.active || m_Base < 0)
            return;
        try {
            long a = m_Base + TIMER;
            if (!readable(a + 52, 12))
                return;
            s_PokeInt.invoke(null, a + 52, (int) m_Restore, false);
            s_PokeInt.invoke(null, a + 56, (int) (m_Restore >>> 32), false);
            s_PokeInt.invoke(null, a + 60, 1, false);
            Log.i(TAG, "tempo da partida restaurado: " + m_Restore + " ms");
        } catch (Exception e) {
            Log.w(TAG, "restaurar tempo", e);
        }
        m_Restore = -1;
    }

    Hud(Activity activity, Cursor cursor, Handler main) {
        s_Self = this;
        m_Prefs = activity.getSharedPreferences("boz_tv", Activity.MODE_PRIVATE);
        s_TimerShown = m_Prefs.getBoolean("timerShown", true);
        m_Cursor = cursor;
        m_Main = main;
        m_View = new HudView(activity);
        activity.addContentView(m_View, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        HandlerThread t = new HandlerThread("BozHud");
        t.start();
        m_Worker = new Handler(t.getLooper());
        m_Worker.post(m_Poll);
    }

    private final Runnable m_Poll = new Runnable() {
        @Override
        public void run() {
            // o jogador fica guardado no banco depois da partida: numa tela de fora dela o banco é zerado, e o
            // reset de inventário da próxima partida (patch_s3e.py) grava o jogador novo
            String screen = m_Cursor.screenNow();
            checkBase();
            if (Cursor.outsideMatch(screen)) {
                forgetPlayer();
                m_Leaving = false;
                s_Round = -1;
                s_Map = null;
                s_DeadOps = false;
                m_MatchLog = null;
            } else if ("savequit".equals(screen) || "quitconfirm".equals(screen)) {
                m_Leaving = true;   // saindo: o carregamento até o menu principal é uma tela desconhecida
            } else if ("pause".equals(screen)) {
                m_Leaving = false;  // desistiu de sair
            }
            int[] c = read();
            // no gameplay também: o Cursor precisa saber se é Dead Ops antes do primeiro pause (Start)
            if ("pause".equals(screen) || (screen == null && !m_Cursor.isMenuMode())) readMatch();
            pauseTimer(screen != null || m_Cursor.isMenuMode());
            boolean show = c != null && screen == null && !m_Leaving && !m_Cursor.isMenuMode();
            TimerSnapshot timer = readTimer();
            restoreTimer(timer);
            if (timer != null && timer.active)
                m_LastElapsed = timer.elapsedMs;
            boolean showTimer = s_TimerShown && timer != null && timer.active && screen == null
                    && !m_Leaving && !m_Cursor.isMenuMode();
            boolean mainTotal = timer != null && "main".equals(screen);
            boolean profileTotal = timer != null && timer.profile && !timer.active && !mainTotal;
            TimerSnapshot previous = m_Timer;
            boolean timerChanged = timer == null ? previous != null : !timer.sameDisplay(previous);
            m_Timer = timer;
            int[] old = m_Counts;
            if (c == null)
                c = new int[]{-1, -1, -1, -1, -1};
            if (!java.util.Arrays.equals(c, old) || show != m_View.m_Show || timerChanged
                    || showTimer != m_View.m_ShowTimer || mainTotal != m_View.m_MainTotal
                    || profileTotal != m_View.m_ProfileTotal) {
                m_Counts = c;
                final boolean s = show;
                m_Main.post(() -> {
                    m_View.m_Show = s;
                    m_View.m_ShowTimer = showTimer;
                    m_View.m_MainTotal = mainTotal;
                    m_View.m_ProfileTotal = profileTotal;
                    m_View.invalidate();
                });
            }
            m_Worker.postDelayed(this, POLL_MS);
        }
    };

    /** Quantidades atuais ou null se não há partida (sem jogador ou memória ilegível). */
    private int[] read() {
        try {
            if (m_Base < 0) {
                long now = android.os.SystemClock.uptimeMillis();
                if (now < m_NextSearch)
                    return null;
                m_NextSearch = now + 2000;
                if (!findBase())
                    return null;
            }
            long player = u32(m_Base + BANK + 8);
            if (player == 0)
                return null;
            int[] c = {-1, -1, -1, -1, -1};
            put(c, u32(player + 0x1ec), (int) u32(player + 0x1cc));
            put(c, u32(player + 0x1f0), (int) u32(player + 0x1d0));
            return c;
        } catch (Exception e) {
            if (!m_Warned) {
                m_Warned = true;
                Log.w(TAG, "leitura", e);
            }
            return null;
        }
    }

    /** Snapshot publicado só pela thread nativa; nenhuma escrita Java nas estatísticas. */
    private TimerSnapshot readTimer() {
        try {
            if (m_Base < 0 || !readable(m_Base + TIMER, 64))
                return null;
            long address = m_Base + TIMER;
            long sequence = u32(address + 4);
            if ((sequence & 1) != 0)
                return m_Timer;
            // Full fences pair with native DMB around the sequence publication.
            m_TimerFence.incrementAndGet();
            byte[] bytes = new byte[64];
            s_PeekBytes.invoke(null, address, bytes, 0, bytes.length);
            m_TimerFence.incrementAndGet();
            java.nio.ByteBuffer b = java.nio.ByteBuffer.wrap(bytes).order(java.nio.ByteOrder.LITTLE_ENDIAN);
            if (b.getInt(0) != TIMER_MAGIC || (b.getInt(4) & 0xffffffffL) != sequence
                    || sequence != u32(address + 4))
                return m_Timer;
            int status = b.getInt(36);
            if (status != m_LastSaveStatus) {
                m_LastSaveStatus = status;
                if (status != 0)
                    Log.w(TAG, "tempo: save nativo falhou, código " + status);
            }
            long elapsed = (b.getInt(12) & 0xffffffffL) | ((b.getInt(16) & 0xffffffffL) << 32);
            long total = (b.getInt(20) & 0x7fffffffL) * 1000L + (b.getInt(24) & 0xffffffffL);
            return new TimerSnapshot(b.getInt(8) == 1, b.getInt(32) == 1,
                    formatTime(Math.max(0, elapsed), false), formatTime(total, true), status, Math.max(0, elapsed));
        } catch (Exception e) {
            if (!m_WarnedTimer) {
                m_WarnedTimer = true;
                Log.w(TAG, "leitura do timer", e);
            }
            return null;
        }
    }

    private boolean m_WarnedTimer;

    /** Round e mapa da partida; em caso de falha mantém o último valor. */
    private void readMatch() {
        try {
            if (m_Base < 0)
                return;
            long waves = u32(m_Base + WAVES);
            int round = waves != 0 ? (int) u32(waves + 0x78) : -1;
            if (waves != 0)
                s_Round = round;
            long level = u32(m_Base + LEVEL);
            if (level != 0 && readable(level + 0x55, 32)) {
                byte[] b = new byte[32];
                s_PeekBytes.invoke(null, level + 0x55, b, 0, b.length);
                int n = 0;
                while (n < b.length && b[n] != 0)
                    n++;
                s_Map = new String(b, 0, n, java.nio.charset.StandardCharsets.US_ASCII);
            }
            String raw = s_Map;
            // Dead Ops não tem CWaveManager nem nome em +0x55; a reserva do Continue vale só para os clássicos
            s_DeadOps = waves == 0 && level != 0;
            if (s_DeadOps)
                s_Map = "deadops";
            else if (s_Map == null || s_Map.isEmpty())
                s_Map = m_ContinueMap;
            if (s_Map != null && !s_Map.isEmpty())
                m_LastMap = s_Map;
            String log = "waves=" + Long.toHexString(waves) + " round=" + round + " level=" + Long.toHexString(level)
                    + " mapa='" + raw + "' -> " + s_Map;
            if (!log.equals(m_MatchLog)) {
                m_MatchLog = log;
                Log.i(TAG, "partida: " + log);
            }
        } catch (Exception e) {
            if (!m_WarnedMatch) {
                m_WarnedMatch = true;
                Log.w(TAG, "leitura da partida", e);
            }
        }
    }

    private boolean m_WarnedMatch;

    static String formatTime(long millis, boolean hoursAlways) {
        long seconds = millis / 1000;
        if (hoursAlways || seconds >= 3600)
            return String.format(java.util.Locale.ROOT, "%02d:%02d:%02d", seconds / 3600,
                    seconds / 60 % 60, seconds % 60);
        return String.format(java.util.Locale.ROOT, "%02d:%02d", seconds / 60, seconds % 60);
    }

    private static final class TimerSnapshot {
        final boolean active, profile;
        final String elapsed, total;
        final int status;
        final long elapsedMs;

        TimerSnapshot(boolean active, boolean profile, String elapsed, String total, int status, long elapsedMs) {
            this.elapsedMs = elapsedMs;
            this.active = active;
            this.profile = profile;
            this.elapsed = elapsed;
            this.total = total;
            this.status = status;
        }

        boolean sameDisplay(TimerSnapshot other) {
            return other != null && active == other.active && profile == other.profile
                    && status == other.status && elapsed.equals(other.elapsed) && total.equals(other.total);
        }
    }

    private boolean m_Warned;

    /**
     * A imagem do jogo pode sair da memória (fim da partida, fechar o jogo) e a lista de /proc/self/maps é relida só
     * a cada 3 s: com m_Base velho, peekInt derrubava o processo (SIGSEGV em forgetPlayer). Relê a lista e confere a
     * assinatura antes das leituras da rodada; se mudou, a imagem é procurada de novo.
     */
    private void checkBase() {
        if (m_Base < 0)
            return;
        try {
            m_MapsAt = 0;
            long sig = m_Base + SIG_OFFSET;
            byte[] b = new byte[SIG.length];
            if (readable(sig, b.length)) {
                s_PeekBytes.invoke(null, sig, b, 0, b.length);
                if (java.util.Arrays.equals(b, SIG))
                    return;
            }
        } catch (Exception e) {
            Log.w(TAG, "conferir imagem", e);
        }
        Log.i(TAG, "imagem do jogo saiu de " + Long.toHexString(m_Base));
        m_Base = -1;
        m_TimerPaused = false;
    }

    private boolean m_TimerPaused;

    /** Flag de pause do timer nativo (state+48, timer.s): com um menu aberto o tempo da partida não avança. */
    private void pauseTimer(boolean paused) {
        if (paused == m_TimerPaused || m_Base < 0)
            return;
        try {
            if (readable(m_Base + TIMER + 48, 4)) {
                s_PokeInt.invoke(null, m_Base + TIMER + 48, paused ? 1 : 0, false);
                m_TimerPaused = paused;
            }
        } catch (Exception e) {
            Log.w(TAG, "pausar timer", e);
        }
    }

    private void forgetPlayer() {
        try {
            if (m_Base >= 0 && readable(m_Base + BANK + 8, 4) && u32(m_Base + BANK + 8) != 0)
                s_PokeInt.invoke(null, m_Base + BANK + 8, 0, false);
        } catch (Exception e) {
            Log.w(TAG, "zerar jogador", e);
        }
    }

    private void put(int[] c, long item, int count) throws Exception {
        if (item == 0)
            return;
        int h = (int) u32(item + 0x5c);
        int i = h == FRAG ? 0 : h == MONKEY ? 1 : h == CLAYMORE ? 2 : h == GERSH ? 3 : h == MATRYOSHKA ? 4 : -1;
        if (i >= 0 && count >= 0 && count < 100)
            c[i] = count;
    }

    private long u32(long addr) throws Exception {
        if (!readable(addr, 4))
            throw new IllegalStateException("endereço fora das áreas mapeadas");
        return ((Integer) s_PeekInt.invoke(null, addr, false)) & 0xffffffffL;
    }

    /** Áreas legíveis de /proc/self/maps (relidas a cada 3 s): ler fora delas derrubaria o jogo. */
    private boolean readable(long addr, int n) throws Exception {
        long now = android.os.SystemClock.uptimeMillis();
        if (now >= m_MapsAt) {
            m_MapsAt = now + 3000;
            java.util.ArrayList<long[]> maps = new java.util.ArrayList<>();
            try (BufferedReader r = new BufferedReader(new FileReader("/proc/self/maps"))) {
                String line;
                while ((line = r.readLine()) != null) {
                    String[] f = line.split("\\s+");
                    if (f.length < 2 || f[1].charAt(0) != 'r')
                        continue;
                    String[] ab = f[0].split("-");
                    maps.add(new long[]{Long.parseLong(ab[0], 16), Long.parseLong(ab[1], 16), f[1].startsWith("rwx") ? 1 : 0});
                }
            }
            m_Maps = maps;
        }
        for (long[] m : m_Maps)
            if (addr >= m[0] && addr + n <= m[1])
                return true;
        return false;
    }

    /**
     * A imagem do jogo fica numa área anônima rwx; acha o início pela assinatura. O Android desta TV não deixa
     * abrir /proc/self/mem, então a leitura usa libcore.io.Memory (API interna liberada para targetSdk 23).
     */
    private boolean findBase() throws Exception {
        if (s_PeekInt == null) {
            Class<?> mem = Class.forName("libcore.io.Memory");
            s_PeekInt = mem.getMethod("peekInt", long.class, boolean.class);
            s_PeekBytes = mem.getMethod("peekByteArray", long.class, byte[].class, int.class, int.class);
            s_PokeInt = mem.getMethod("pokeInt", long.class, int.class, boolean.class);
        }
        m_MapsAt = 0;
        readable(0, 0);
        for (long[] m : m_Maps) {
            if (m[2] == 0 || m[1] - m[0] < 0x400000)
                continue;
            byte[] all = new byte[(int) (m[1] - m[0])];
            s_PeekBytes.invoke(null, m[0], all, 0, all.length);
            for (int i = 0; i + SIG.length <= all.length; i += 2) {
                int k = 0;
                while (k < SIG.length && all[i + k] == SIG[k])
                    k++;
                if (k == SIG.length) {
                    m_Base = m[0] + i - SIG_OFFSET;
                    Log.i(TAG, "imagem do jogo em " + Long.toHexString(m_Base));
                    return true;
                }
            }
        }
        return false;
    }

    private final class HudView extends View {
        private final Bitmap[] m_Icons = new Bitmap[ITEMS];
        private final Bitmap[] m_Digits = new Bitmap[10];   // fonte do HUD do jogo (bo_hud30pt_p), com contorno
        private final Paint m_Bmp = new Paint(Paint.FILTER_BITMAP_FLAG);
        private final Paint m_Text = new Paint(Paint.ANTI_ALIAS_FLAG);
        boolean m_Show, m_ShowTimer, m_MainTotal, m_ProfileTotal;
        private android.graphics.Typeface m_Comic;

        HudView(Context ctx) {
            super(ctx);
            setWillNotDraw(false);
            setFocusable(false);
            try {
                m_Comic = android.graphics.Typeface.createFromAsset(ctx.getAssets(), "character-menu/comic.ttf");
            } catch (Exception e) {
                m_Comic = android.graphics.Typeface.DEFAULT_BOLD;
            }
            String[] names = {"frag", "monkey", "claymore", "gersch", "matryoshka"};
            for (int i = 0; i < ITEMS; i++) {
                try {
                    m_Icons[i] = BitmapFactory.decodeStream(ctx.getAssets().open("hud/" + names[i] + ".png"));
                } catch (Exception e) {
                    Log.e(TAG, "ícone " + names[i], e);
                }
            }
            for (int i = 0; i < 10; i++) {
                try {
                    m_Digits[i] = BitmapFactory.decodeStream(ctx.getAssets().open("hud/d" + i + ".png"));
                } catch (Exception e) {
                    Log.e(TAG, "dígito " + i, e);
                }
            }
        }

        private void drawTime(Canvas c, String value, float right, float baseline, float size) {
            m_Text.setTypeface(android.graphics.Typeface.MONOSPACE);
            m_Text.setTextSize(size);
            m_Text.setTextAlign(Paint.Align.RIGHT);
            float width = m_Text.measureText(value);
            m_Text.setColor(0xbb111111);
            c.drawRoundRect(new RectF(right - width - 12, baseline - size - 6,
                    right + 12, baseline + 8), 6, 6, m_Text);
            m_Text.setColor(0xfff2eee4);
            c.drawText(value, right, baseline, m_Text);
        }

        /** Timer da partida com a fonte do HUD do jogo (dígitos d0-d9); o ":" é desenhado no mesmo estilo. */
        private void drawHudTime(Canvas c, String s, float right, float top) {
            final float h = 34, colon = h * 0.34f;
            float width = 0;
            for (char ch : s.toCharArray()) {
                if (ch >= '0' && ch <= '9' && m_Digits[ch - '0'] != null) {
                    Bitmap d = m_Digits[ch - '0'];
                    width += h * d.getWidth() / (float) d.getHeight() * 0.82f;
                } else if (ch == ':') {
                    width += colon;
                }
            }
            float x = right - width;
            for (char ch : s.toCharArray()) {
                if (ch >= '0' && ch <= '9' && m_Digits[ch - '0'] != null) {
                    Bitmap d = m_Digits[ch - '0'];
                    float w = h * d.getWidth() / (float) d.getHeight();
                    c.drawBitmap(d, null, new RectF(x, top, x + w, top + h), m_Bmp);
                    x += w * 0.82f;
                } else if (ch == ':') {
                    float cx = x + colon * 0.5f, r = h * 0.075f;
                    for (float cy : new float[]{top + h * 0.36f, top + h * 0.72f}) {
                        float sx = cx - (cy - top) * 0.12f;   // itálico como os dígitos
                        m_Text.setStyle(Paint.Style.FILL);
                        m_Text.setColor(0xff111111);
                        c.drawCircle(sx + 2, cy + 2, r + 2.5f, m_Text);   // sombra
                        c.drawCircle(sx, cy, r + 2.5f, m_Text);           // contorno
                        m_Text.setColor(0xffffffff);
                        c.drawCircle(sx, cy, r, m_Text);
                    }
                    x += colon;
                }
            }
        }

        private void comic(Canvas c, String s, float right, float baseline, float size, int color) {
            m_Text.setTypeface(m_Comic);
            m_Text.setTextSize(size);
            m_Text.setTextAlign(Paint.Align.RIGHT);
            m_Text.setStyle(Paint.Style.STROKE);
            m_Text.setStrokeWidth(size * 0.16f);
            m_Text.setStrokeJoin(Paint.Join.ROUND);
            m_Text.setColor(0xff111111);
            c.drawText(s, right, baseline, m_Text);
            m_Text.setStyle(Paint.Style.FILL);
            m_Text.setColor(color);
            c.drawText(s, right, baseline, m_Text);
        }

        /** Player Profile: TIME PLAYED no quadro STATISTICS, abaixo de HIGHEST WAVE, no estilo dos textos do SWF. */
        private void drawProfileTotal(Canvas c, String total) {
            int save = c.save();
            c.rotate(2.7f, 1088, 520);   // mesma inclinação de BEST SCORE / HIGHEST WAVE
            comic(c, "TIME PLAYED:", 1088, 516, 24, 0xfff2eee4);
            comic(c, total, 1084, 550, 28, 0xfff5c518);
            c.restoreToCount(save);
        }

        @Override
        protected void onDraw(Canvas c) {
            if (!m_Show && !m_ShowTimer && !m_MainTotal && !m_ProfileTotal)
                return;
            // Mantém o HUD inteiro dentro da área de referência, inclusive fora de 16:9.
            float s = Math.min(getWidth() / 1280f, getHeight() / 720f);
            if (s <= 0)
                return;
            int save = c.save();
            c.translate((getWidth() - 1280f * s) / 2f, (getHeight() - 720f * s) / 2f);
            int[] counts = m_Counts;
            float y = HUD_Y;
            for (int i = 0; m_Show && i < ITEMS; i++) {
                if (counts[i] < 0)
                    continue;
                Bitmap b = m_Icons[i];
                if (b != null) {
                    // cabe numa caixa ICON x ICON*1.3 (o do Gersh é largo), centrado na linha
                    float k = Math.min(ICON / b.getHeight(), ICON * 1.3f / b.getWidth());
                    float w = b.getWidth() * k, h = b.getHeight() * k, top = y + (ICON - h) / 2;
                    c.drawBitmap(b, null, new RectF(HUD_X * s, top * s, (HUD_X + w) * s, (top + h) * s), m_Bmp);
                }
                float tx = HUD_X + ICON * 1.3f + 6;   // mesma coluna de números de antes (1180 + 44 + 6)
                for (char ch : String.valueOf(counts[i]).toCharArray()) {
                    Bitmap d = m_Digits[ch - '0'];
                    if (d == null)
                        continue;
                    float w = DIGIT_H * d.getWidth() / (float) d.getHeight();
                    float top = y + (ICON - DIGIT_H) / 2;
                    c.drawBitmap(d, null, new RectF(tx * s, top * s, (tx + w) * s, (top + DIGIT_H) * s), m_Bmp);
                    tx += w * 0.82f;   // a fonte é itálica com sombra: os dígitos se encostam como no HUD do jogo
                }
                y += ROW;
            }
            TimerSnapshot timer = m_Timer;
            if (timer != null) {
                int textSave = c.save();
                c.scale(s, s);
                if (m_ShowTimer)
                    drawHudTime(c, timer.elapsed, TIMER_X, TIMER_Y);
                if (m_MainTotal)
                    drawTime(c, "TIME PLAYED  " + timer.total, 858, 430, 18);
                if (m_ProfileTotal)
                    drawProfileTotal(c, timer.total);
                if (m_MainTotal && timer.status != 0)
                    drawTime(c, "TIME NOT SAVED YET", 1232, 86, 18);
                if (m_ProfileTotal && timer.status != 0)
                    comic(c, "NOT SAVED YET", 1084, 590, 20, 0xfff2eee4);
                c.restoreToCount(textSave);
            }
            c.restoreToCount(save);
        }
    }
}
