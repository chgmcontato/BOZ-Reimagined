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
    private static final long POLL_MS = 125;

    private final Cursor m_Cursor;
    private final HudView m_View;
    private final Handler m_Main;
    private final Handler m_Worker;
    private static java.lang.reflect.Method s_PeekInt, s_PeekBytes, s_PokeInt, s_PokeByte;
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
    // contador de FPS (botão FPS do pause): mesmo estilo do PC (fonte em quadrinhos, amarelo, rodapé)
    static volatile boolean s_FpsShown = true;
    static volatile int s_Fps = -1;
    static void toggleFps() {
        Hud h = s_Self;
        s_FpsShown = !s_FpsShown;
        if (h != null) {
            h.m_Prefs.edit().putBoolean("fpsShown", s_FpsShown).apply();
            h.m_Main.post(h.m_View::invalidate);
        }
    }
    private long m_NextFpsRead;

    // ---- scoreboard (SELECT segurado), igual ao do PC: src/reimagined.c, comentário "Scoreboard"
    private static final long SB_SCORES = 0x496110, SB_STATS = 0x49610c, SB_REMOTES = 0x4a28a8;
    private static final int SB_START = 500, SB_UNKNOWN = -1, SB_MAX = 4;
    private static final class SbRow {
        String name; int character, score = SB_START, kills = SB_UNKNOWN, downs = SB_UNKNOWN, revives = SB_UNKNOWN, headshots = SB_UNKNOWN;
        boolean local;
    }
    private static volatile boolean s_SbAvailable, s_SbHeld;
    private volatile SbRow[] m_SbRows = new SbRow[0];

    /** Só no gameplay de um mapa clássico: fora disso o SELECT continua alternando o modo menu. */
    static boolean scoreboardAvailable() { return s_SbAvailable; }

    static void setScoreboard(boolean held) {
        Hud h = s_Self;
        s_SbHeld = held;
        if (h != null) {
            if (held) h.m_Worker.post(h::readScoreboard);
            h.m_Main.post(h.m_View::invalidate);
        }
    }

    /** IwHashString: djb2 da string em minúsculas. */
    private static int sbHash(String s) {
        int h = 5381;
        for (int i = 0; i < s.length(); i++) {
            int c = s.charAt(i);
            if (c >= 'A' && c <= 'Z') c += 32;
            h = h * 33 + c;
        }
        return h;
    }

    /** Percorre uma árvore do STLport (cabeçalho +4 raiz; nó +8 esq., +0xc dir., +0x10 chave, +0x14 valor). */
    private int sbNodes(long header, long[] out) throws Exception {
        long[] stack = new long[24];
        long node = header != 0 ? u32(header + 4) : 0;
        int depth = 0, count = 0, steps = 0;
        while ((node != 0 || depth > 0) && count < out.length && steps++ < 256) {
            while (node != 0 && depth < 24 && steps++ < 256) {
                stack[depth++] = node;
                node = u32(node + 8);
            }
            if (depth == 0) break;
            node = stack[--depth];
            out[count++] = node;
            node = u32(node + 0xc);
        }
        return count;
    }

    private int sbStat(long header, String name) throws Exception {
        long[] nodes = new long[64];
        int n = sbNodes(header, nodes), key = sbHash(name);
        for (int i = 0; i < n; i++)
            if ((int) u32(nodes[i] + 0x10) == key) return (int) u32(nodes[i] + 0x14);
        return 0;
    }

    private void readScoreboard() {
        if (!s_SbHeld || m_Base < 0) return;
        try {
            long scores = u32(m_Base + SB_SCORES), stats = u32(m_Base + SB_STATS), level = u32(m_Base + LEVEL);
            if (scores == 0 || stats == 0 || level == 0) return;
            boolean coop = (u32(level + 0x208) & 0xff) != 0;
            long[] nodes = new long[16];
            int scoreCount = sbNodes(u32(scores + 0x40), nodes);
            boolean[] used = new boolean[16];
            java.util.ArrayList<SbRow> rows = new java.util.ArrayList<>();
            if (coop) {
                long[] remotes = new long[SB_MAX - 1];
                int n = sbNodes(u32(m_Base + SB_REMOTES + 4), remotes);
                for (int i = 0; i < n; i++) {
                    long info = u32(u32(remotes[i] + 0x14) + 0x118), player = u32(info);
                    if (player == 0) continue;
                    byte[] b = new byte[15];
                    if (!readable(player + 0xc, b.length)) continue;
                    s_PeekBytes.invoke(null, player + 0xc, b, 0, b.length);
                    int len = 0;
                    while (len < b.length && b[len] >= 0x20 && b[len] <= 0x7e) len++;
                    SbRow r = new SbRow();
                    r.name = new String(b, 0, len, java.nio.charset.StandardCharsets.US_ASCII);
                    r.character = (int) u32(info + 4);
                    int key = sbHash(r.name);
                    for (int s = 0; s < scoreCount; s++)
                        if ((int) u32(nodes[s] + 0x10) == key) {
                            r.score = (int) u32(nodes[s] + 0x14);
                            used[s] = true;
                        }
                    r.name = r.name.toUpperCase(java.util.Locale.ROOT);
                    int[] heard = m_CoopStats.lookup(key);
                    if (heard != null) {
                        r.kills = heard[0];
                        r.downs = heard[1];
                        r.revives = heard[2];
                        r.headshots = heard[3];
                    }
                    rows.add(r);
                }
            }
            SbRow me = new SbRow();
            me.local = true;
            me.name = PlayerNames.get(m_View.getContext(), 0).toUpperCase(java.util.Locale.ROOT);
            me.character = (int) u32(level + 0x210);
            for (int s = 0; s < scoreCount; s++)
                if (!used[s]) {   // no solo é o único nó, seja qual for o nome interno do jogador
                    me.score = (int) u32(nodes[s] + 0x14);
                    break;
                }
            long bucket = u32(stats + (coop ? 2 : 1) * 0xc + 0x10);
            me.kills = sbStat(bucket, "STAT_ZOMBIESKILLED");
            me.headshots = sbStat(bucket, "STAT_HEADSHOTS");
            me.revives = sbStat(bucket, "STAT_REVIVED");
            me.downs = sbStat(bucket, "STAT_PLAYERDEATHS");
            rows.add(me);
            java.util.Collections.sort(rows, (a, b) -> Integer.compare(b.score, a.score));
            m_SbRows = rows.toArray(new SbRow[0]);
            m_Main.post(m_View::invalidate);
        } catch (Exception e) {
            if (!m_WarnedSb) { m_WarnedSb = true; Log.w(TAG, "leitura do scoreboard", e); }
        }
    }
    private boolean m_WarnedSb;
    private final CoopStats m_CoopStats = new CoopStats();
    private int m_Party;   // mais jogadores vistos na partida (as conexões podem incluir entradas antigas)

    /** Jogadores reais para o código das ondas; antes de os remotos existirem, a rede só diz "ao menos dois". */
    private void syncWaveParty(int remotesPlusMe) throws Exception {
        long addr = s_WavePartyAddr;
        if (addr == 0) return;
        int n = Math.max(remotesPlusMe, m_Party);
        if (n < 2) n = 2;   // só chega aqui em co-op
        n = Math.min(n, 4);
        if (n != s_WavePartyValue) {
            s_PokeInt.invoke(null, addr, n, false);
            s_WavePartyValue = n;
            Log.i(TAG, "zumbis: ondas para " + n + " jogadores");
        }
    }

    /** Co-op, a cada rodada do poll: publica os nossos números e mantém o tamanho real do grupo. */
    private void coopFrame(boolean active) {
        if (!active) { m_Party = 0; return; }
        if (m_Base < 0) return;
        try {
            long stats = u32(m_Base + SB_STATS), level = u32(m_Base + LEVEL);
            if (stats == 0 || level == 0 || (u32(level + 0x208) & 0xff) == 0) return;
            long bucket = u32(stats + 2 * 0xc + 0x10);
            m_CoopStats.publish(sbHash(PlayerNames.get(m_View.getContext(), 0)),
                    sbStat(bucket, "STAT_ZOMBIESKILLED"), sbStat(bucket, "STAT_PLAYERDEATHS"),
                    sbStat(bucket, "STAT_REVIVED"), sbStat(bucket, "STAT_HEADSHOTS"));
            int party = sbNodes(u32(m_Base + SB_REMOTES + 4), new long[SB_MAX - 1]) + 1;
            if (party > m_Party) m_Party = party;
            syncWaveParty(party);
        } catch (Exception e) {
            if (!m_WarnedSb) { m_WarnedSb = true; Log.w(TAG, "estatísticas do co-op", e); }
        }
    }

    /**
     * FPS medido pelo próprio motor (o que IwGxPrintFrameRate mostrava): tempo do quadro em [[base+0x44757c]]+0x588,
     * em ms inteiros (<<12) ou ponto fixo 12.12; FPS = 1000/ms.
     */
    private void readFps() {
        long now = android.os.SystemClock.uptimeMillis();
        if (now < m_NextFpsRead || m_Base < 0) return;
        m_NextFpsRead = now + 500;
        try {
            long g = u32(m_Base + 0x44757c);
            long st = g == 0 ? 0 : u32(g);
            long v = st == 0 ? 0 : u32(st + 0x588);
            int fps = -1;
            if (v != 0) fps = (v & 0xfff) == 0 ? (int) Math.round(1000.0 / (v >> 12)) : (int) Math.round(4096000.0 / v);
            if (fps > 999) fps = 999;
            if (fps != s_Fps) {
                s_Fps = fps;
                if (s_FpsShown) m_Main.post(m_View::invalidate);
            }
        } catch (Exception e) { s_Fps = -1; }
    }
    /** Esconde o contador nativo do IwGx (fonte de debug); o nosso desenha o mesmo número. */
    private static void hideNativeFps(long base, long region, byte[] all) {
        try {
            int i = (int) (base + 0x2ad888 - region);
            int w = (all[i] & 0xff) | (all[i + 1] & 0xff) << 8 | (all[i + 2] & 0xff) << 16 | (all[i + 3] & 0xff) << 24;
            if (w != 0xe59f307c) { Log.i(TAG, "FPS: contador nativo não encontrado"); return; }
            s_PokeInt.invoke(null, base + 0x2ad888, 0xe12fff1e, false);   // bx lr (ARM)
            Log.i(TAG, "FPS: contador nativo trocado pelo do Hud");
        } catch (Exception e) { Log.w(TAG, "FPS: contador nativo", e); }
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
        s_FpsShown = m_Prefs.getBoolean("fpsShown", true);
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
            // Updated below after reading the native match-active snapshot.
            boolean show = c != null && screen == null && !m_Leaving && !m_Cursor.isMenuMode();
            TimerSnapshot timer = readTimer();
            readFps();
            syncWaveCoop();
            syncCoopPause(timer != null && timer.active && Menus.coopSession());
            pauseTimer(screen != null || m_Cursor.isMenuMode() || s_WaitingPlayer);
            if (timer != null && timer.active && screen == null && !m_Leaving)
                m_Main.post(m_Cursor::onMatchActive);
            restoreTimer(timer);
            coopFrame(timer != null && timer.active);
            boolean sbWas = s_SbAvailable;
            s_SbAvailable = timer != null && timer.active && screen == null && !m_Leaving
                    && !m_Cursor.isMenuMode() && !s_DeadOps;
            if (sbWas != s_SbAvailable)
                m_Main.post(m_View::invalidate);
            if (s_SbAvailable)
                readScoreboard();
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
    private final byte[] m_SigBuf = new byte[SIG.length];
    private void checkBase() {
        if (m_Base < 0)
            return;
        try {
            // Antes zerava m_MapsAt aqui: o /proc/self/maps era relido 8x por segundo e a thread BozHud
            // gastava ~90% de um núcleo (medido na TV). Só força a releitura se a assinatura não estiver
            // numa área conhecida.
            long sig = m_Base + SIG_OFFSET;
            byte[] b = m_SigBuf;
            if (!readable(sig, b.length)) {
                m_MapsAt = 0;
            }
            if (readable(sig, b.length)) {
                s_PeekBytes.invoke(null, sig, b, 0, b.length);
                if (java.util.Arrays.equals(b, SIG))
                    return;
            }
        } catch (Exception e) {
            Log.w(TAG, "conferir imagem", e);
        }
        Log.i(TAG, "imagem do jogo saiu de " + Long.toHexString(m_Base));
        s_WaveCoopAddr = 0;
        m_Base = -1;
        m_TimerPaused = false;
    }

    static volatile boolean s_WaitingPlayer;
    /** Jogadores presentes / esperados, para o cartão de partida pausada. */
    static volatile int s_WaitPresent = 1, s_WaitExpected = 2;
    private final CoopPauseState m_CoopPause = new CoopPauseState();
    private long m_OwnedPause;
    private long nativeIngameState() throws Exception {
        long app=u32(m_Base+0x491784);
        if(app==0 || u32(app)!=m_Base+0x42f250) return 0;
        long entries=u32(app+0x44), count=u32(app+0x48);
        if(entries==0 || count>128) return 0;
        for(int i=0;i<count;i++) if(u32(entries+i*8)==0) {
            long wrapper=u32(entries+i*8+4);
            long state=wrapper==0?0:u32(wrapper+4);
            return state!=0 && u32(state)==m_Base+0x42f708?state:0;
        }
        return 0;
    }
    private void syncCoopPause(boolean active) {
        if(m_Base<0)return;
        try {
            long state=nativeIngameState();
            long network=u32(m_Base+0x4958d4);
            if(active && (state==0 || network==0))return;
            boolean waiting=m_CoopPause.update(active,state,active?(int)u32(network+0x64):0);
            if(waiting && state!=0 && (u32(state+0x50)&255)==0) { // reaplica se o pause nativo for fechado
                s_PokeByte.invoke(null,state+0x50,(byte)1);
                m_OwnedPause=state;
            } else if(!waiting && m_OwnedPause!=0) {
                if(state==m_OwnedPause && (u32(state+0x50)&255)==1)
                    s_PokeByte.invoke(null,state+0x50,(byte)0);
                m_OwnedPause=0;
            }
            // network+0x64 conta conexões, que podem incluir uma antiga: só a queda é confiável
            s_WaitExpected=Math.max(2,m_Party);
            s_WaitPresent=Math.max(1,s_WaitExpected-Math.max(1,m_CoopPause.peak()-m_CoopPause.peers()));
            if(waiting!=s_WaitingPlayer) {
                s_WaitingPlayer=waiting;
                Log.i(TAG,"Co-op: "+(waiting?"partida congelada, aguardando jogador":"jogadores presentes, partida liberada"));
                m_Main.post(m_View::invalidate);
            }
        } catch(Exception e) {Log.w(TAG,"estado da pausa cooperativa",e);}
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
                    // "start-end perms ...": sem regex, para não gerar lixo
                    int dash = line.indexOf('-'), sp = line.indexOf(' ');
                    if (dash <= 0 || sp <= dash || sp + 4 > line.length() || line.charAt(sp + 1) != 'r')
                        continue;
                    boolean rwx = line.charAt(sp + 2) == 'w' && line.charAt(sp + 3) == 'x';
                    maps.add(new long[]{Long.parseLong(line.substring(0, dash), 16), Long.parseLong(line.substring(dash + 1, sp), 16), rwx ? 1 : 0});
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
            s_PokeByte = mem.getMethod("pokeByte", long.class, byte.class);
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
                    unlockFrameRate(m[0], all);
                    patchWaves(m[0], all, m_Base);
                    hideNativeFps(m_Base, m[0], all);
                    return true;
                }
            }
        }
        return false;
    }

    /**
     * O jogo se limita a 30 FPS: uma função chamada a cada quadro gira em s3eTimerGetMs/s3eDeviceYield até passarem
     * 33/33/34 ms. A física usa o tempo medido do quadro, então sem ela o limite passa a ser só a tela (vsync).
     * Achada pelo padrão "cmp r3,#2; ite eq; movs r3,#34; movs r3,#33"; troca o push inicial por "bx lr" com uma
     * escrita única de 16 bits (nunca fica meia instrução visível para o processador).
     */
    private static void unlockFrameRate(long base, byte[] all) {
        final byte[] sig = {0x02, 0x2b, 0x0c, (byte) 0xbf, 0x22, 0x23, 0x21, 0x23};
        try {
            for (int i = 64; i + sig.length <= all.length; i += 2) {
                int k = 0;
                while (k < sig.length && all[i + k] == sig[k]) k++;
                if (k != sig.length) continue;
                for (int back = 2; back <= 64; back += 2) {
                    int f = i - back;
                    if (all[f + 1] == (byte) 0xb5 && (all[f] & 0x10) != 0) {   // push {r0-r4, lr}
                        // A função começa com o s3eDeviceYield(0) do laço original (é nele que o motor entrega os
                        // toques do Android: analógicos e toques dos menus). Mantém essa parte e troca só a espera:
                        // push; adr.w r4; movs r0,#0; blx yield | aqui (+12): pop {r0-r4,pc}; nop
                        int at = f + 12;
                        long addr = base + at;
                        boolean blx = (all[at + 1] & 0xf8) == 0xf0 && (all[at + 3] & 0xd0) == 0xc0;
                        if ((addr & 3) != 0 || !blx) { Log.i(TAG, "FPS: formato inesperado, limitador mantido"); return; }
                        s_PokeInt.invoke(null, addr, 0xbf00bd1f, false);   // pop {r0-r4,pc}; nop
                        Log.i(TAG, "FPS: limitador de 30 FPS desligado em " + Long.toHexString(base + f) + " (yield do jogo mantido)");
                        return;
                    }
                }
            }
            Log.i(TAG, "FPS: limitador não encontrado, o jogo continua em 30 FPS");
        } catch (Exception e) {
            Log.w(TAG, "FPS: não foi possível desligar o limitador", e);
        }
    }

    /**
     * Zumbis por round no co-op. O patch do Reimagined (patch_s3e.py, "zcount" em 0x410900) usa a conta do BO1
     * SOLO x 15/24 para todos, então o co-op tinha os zumbis do solo. Código Thumb novo em 0x410f00 (área zerada
     * depois dos patches; montado com keystone e testado no Unicorn contra a fórmula do PC), com o número de
     * jogadores da rede do jogo ([0x4958d4]+0x64, 1-4): R1-9 por tabela; R10+ = (2400 + 9 m R^2 + 80) / 160,
     * m = 1/2/4/6 (P = 0,09/0,18/0,36/0,54). zcount vira "ldr.w pc,[pc]; .word novo|1".
     * O blob lê [[literal]+0x64]. O campo real da rede conta conexões (pode incluir uma antiga), então o literal
     * aponta para WAVE_PARTY: um ponteiro falso tal que [ponteiro+0x64] é o número de jogadores escrito pelo Hud.
     */
    private static final int WAVE_FN = 0x410900, WAVE_CODE = 0x410f00, WAVE_LIT = 0x64, WAVE_COOP = 0x68, WAVE_PARTY = 0x411000;
    /** Endereço da chave de co-op do código das ondas (0 = solo: sempre 1 jogador; a rede guarda o co-op anterior). */
    private static volatile long s_WaveCoopAddr, s_WavePartyAddr;
    private static int s_WavePartyValue;
    private static int s_WaveCoopValue = -1;
    private static void syncWaveCoop() {
        long addr = s_WaveCoopAddr;
        int v = Menus.coopSession() ? 1 : 0;
        if (addr == 0 || v == s_WaveCoopValue) return;
        try {
            s_PokeInt.invoke(null, addr, v, false);
            s_WaveCoopValue = v;
            Log.i(TAG, "zumbis: ondas " + (v == 1 ? "do co-op (pelo número de jogadores)" : "do solo"));
        } catch (Exception e) {
            Log.w(TAG, "zumbis: chave do co-op", e);
        }
    }
    private static final byte[] WAVE_BLOB = {16,-75,1,35,24,74,26,-79,22,74,18,104,2,-79,83,110,1,43,-72,-65,1,35,4,43,-56,-65,4,35,91,30,9,41,5,-40,18,-94,10,36,3,-5,4,34,80,92,16,-67,25,-94,-45,92,65,-10,64,114,-111,66,-120,-65,17,70,1,-5,1,-16,9,34,2,-5,3,-14,0,-5,2,-16,64,-10,-80,18,16,68,76,-10,-51,66,-52,-10,-52,66,-96,-5,2,32,-64,9,16,-67,0,-65,0,-65,0,0,0,0,0,0,0,0,4,4,5,8,11,15,17,18,18,18,4,4,6,9,13,17,19,20,21,21,6,6,6,11,16,20,24,25,27,28,6,6,8,13,18,23,28,31,33,35,1,2,4,6};
    private static void patchWaves(long region, byte[] all, long base) {
        try {
            s_WaveCoopAddr = 0;
            s_WaveCoopValue = -1;
            s_WavePartyAddr = 0;
            int fn = (int) (base + WAVE_FN - region), code = (int) (base + WAVE_CODE - region);
            int w0 = (all[fn] & 0xff) | (all[fn + 1] & 0xff) << 8 | (all[fn + 2] & 0xff) << 16 | (all[fn + 3] & 0xff) << 24;
            int w1 = (all[fn + 4] & 0xff) | (all[fn + 5] & 0xff) << 8 | (all[fn + 6] & 0xff) << 16 | (all[fn + 7] & 0xff) << 24;
            if (w0 == 0xf000f8df && w1 == (int) ((base + WAVE_CODE) | 1)) {   // já aplicado nesta imagem
                s_WaveCoopAddr = base + WAVE_CODE + WAVE_COOP;
                s_WavePartyAddr = base + WAVE_PARTY + 4;
                s_WavePartyValue = -1;
                return;
            }
            final byte[] expect = {0x09, 0x29, 0x02, (byte) 0xd8, 0x0b, (byte) 0xa2, 0x50, 0x5c};
            for (int i = 0; i < expect.length; i++)
                if (all[fn + i] != expect[i]) { Log.i(TAG, "zumbis: zcount do Reimagined não encontrado"); return; }
            for (int i = 0; i < WAVE_BLOB.length; i++)
                if (all[code + i] != 0) { Log.i(TAG, "zumbis: área livre ocupada, ondas sem mudança"); return; }
            int party = (int) (base + WAVE_PARTY - region);
            for (int i = 0; i < 8; i++)
                if (all[party + i] != 0) { Log.i(TAG, "zumbis: área do grupo ocupada, ondas sem mudança"); return; }
            s_PokeInt.invoke(null, base + WAVE_PARTY, (int) (base + WAVE_PARTY + 4 - 0x64), false);
            s_PokeInt.invoke(null, base + WAVE_PARTY + 4, 2, false);   // co-op até o Hud confirmar
            s_WavePartyAddr = base + WAVE_PARTY + 4;
            s_WavePartyValue = 2;
            byte[] b = WAVE_BLOB.clone();
            long net = base + WAVE_PARTY;
            for (int k = 0; k < 4; k++) b[WAVE_LIT + k] = (byte) (net >> (8 * k));
            for (int i = 0; i < b.length; i += 4) {
                int w = (b[i] & 0xff) | (b[i + 1] & 0xff) << 8 | (b[i + 2] & 0xff) << 16 | (b[i + 3] & 0xff) << 24;
                s_PokeInt.invoke(null, base + WAVE_CODE + i, w, false);
            }
            s_WaveCoopAddr = base + WAVE_CODE + WAVE_COOP;
            s_WaveCoopValue = 0;
            s_PokeInt.invoke(null, base + WAVE_FN + 4, (int) ((base + WAVE_CODE) | 1), false);
            s_PokeInt.invoke(null, base + WAVE_FN, 0xf000f8df, false);   // ldr.w pc, [pc, #0]
            Log.i(TAG, "zumbis: ondas seguem o número de jogadores (solo 0,09, 2p 0,18, 3p 0,36, 4p 0,54)");
        } catch (Exception e) {
            Log.w(TAG, "zumbis: não foi possível ajustar as ondas", e);
        }
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

        private final Paint m_Card = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final android.graphics.Path m_CardPath = new android.graphics.Path();

        private void quad(Canvas c, float[] p, int color, boolean stroke, float width) {
            m_CardPath.reset();
            m_CardPath.moveTo(p[0], p[1]);
            m_CardPath.lineTo(p[2], p[3]);
            m_CardPath.lineTo(p[4], p[5]);
            m_CardPath.lineTo(p[6], p[7]);
            m_CardPath.close();
            m_Card.setShader(null);
            m_Card.setShadowLayer(0, 0, 0, 0);
            m_Card.setStyle(stroke ? Paint.Style.STROKE : Paint.Style.FILL);
            m_Card.setStrokeWidth(width);
            m_Card.setColor(color);
            c.drawPath(m_CardPath, m_Card);
        }

        /** Cartão de partida pausada no co-op, igual ao do PC (src/reimagined.c draw_coop_pause). */
        private void drawCoopPause(Canvas c) {
            long now = android.os.SystemClock.uptimeMillis();
            m_Card.setStyle(Paint.Style.FILL);
            m_Card.setColor(0x99000000);
            c.drawRect(0, 0, 1280, 720, m_Card);
            float x = 330, y = 230, w = 620, h = 250;
            float[] panel = {x, y + 8, x + w, y, x + w - 4, y + h - 6, x + 3, y + h};
            quad(c, panel, 0xff151211, false, 0);
            quad(c, panel, 0xff151211, true, 5);
            quad(c, panel, 0xff827763, true, 1.5f);
            quad(c, new float[]{x + 24, y + 30, x + w - 30, y + 22, x + w - 34, y + 84, x + 22, y + 90},
                    0xff791d24, false, 0);
            m_Card.setStyle(Paint.Style.FILL);
            m_Card.setTypeface(m_Comic);
            m_Card.setTextAlign(Paint.Align.CENTER);
            m_Card.setColor(0xfff2eee4);
            m_Card.setTextSize(40);
            c.drawText("MATCH PAUSED", 640, y + 72, m_Card);
            m_Card.setTextSize(24);
            String waiting = "WAITING FOR A PLAYER TO RECONNECT";
            float left = 640 - m_Card.measureText(waiting) / 2;
            m_Card.setTextAlign(Paint.Align.LEFT);
            c.drawText(waiting, left, y + 138, m_Card);
            c.drawText("...".substring(0, (int) ((now / 400) % 4)), left + m_Card.measureText(waiting), y + 138, m_Card);
            m_Card.setTextAlign(Paint.Align.CENTER);
            m_Card.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
            m_Card.setTextSize(22);
            m_Card.setColor(0xfff5c518);
            c.drawText("PLAYERS " + s_WaitPresent + " / " + s_WaitExpected, 640, y + 182, m_Card);
            m_Card.setTypeface(android.graphics.Typeface.DEFAULT);
            m_Card.setTextSize(18);
            m_Card.setColor(0xffbcb8ad);
            c.drawText("The match resumes on its own when everyone is back.", 640, y + 220, m_Card);
        }

        private void sbText(Canvas c, String s, float x, float y, float size, int color, Paint.Align align) {
            m_Card.setStyle(Paint.Style.FILL);
            m_Card.setTypeface(m_Comic);
            m_Card.setTextAlign(align);
            m_Card.setTextSize(size);
            m_Card.setColor(color);
            c.drawText(s, x, y, m_Card);
        }

        private void sbValue(Canvas c, int v, float x, float y) {
            sbText(c, v == SB_UNKNOWN ? "-" : String.valueOf(v), x, y, 27, 0xfff2eee4, Paint.Align.CENTER);
        }

        /** Mesma geometria de draw_scoreboard (src/reimagined.c). */
        private void drawScoreboard(Canvas c, SbRow[] rows) {
            final int[] colors = {0xfff2eee4, 0xff74b9e8, 0xfff5c518, 0xff86cf72};
            final String[] heads = {"SCORE", "KILLS", "DOWNS", "REVIVES", "HEADSHOTS"};
            final float[] cols = {560, 680, 790, 900, 1030};
            final int paper = 0xfff2eee4, legend = 0xffd5c9ac, edge = 0xff827763;
            float x = 110, w = 1060, row = 62, h = 196 + rows.length * row, y = 360 - h / 2;
            m_Card.setStyle(Paint.Style.FILL);
            m_Card.setColor(0x66000000);
            c.drawRect(0, 0, 1280, 720, m_Card);
            float[] panel = {x, y + 8, x + w, y, x + w - 4, y + h - 6, x + 3, y + h};
            quad(c, panel, 0xf0151211, false, 0);
            quad(c, panel, 0xff151211, true, 5);
            quad(c, panel, edge, true, 1.5f);
            quad(c, new float[]{x + 24, y + 28, x + w - 30, y + 20, x + w - 34, y + 86, x + 22, y + 92}, 0xff791d24, false, 0);
            sbText(c, "SCOREBOARD", x + 52, y + 72, 42, paper, Paint.Align.LEFT);
            String map = StyledScreen.mapName(s_Map), title = map != null ? map : "";
            if (s_Round > 0) title += (title.isEmpty() ? "" : "  -  ") + "ROUND " + s_Round;
            sbText(c, title, x + w - 56, y + 68, 25, paper, Paint.Align.RIGHT);
            sbText(c, "PLAYER", x + 58, y + 124, 19, legend, Paint.Align.LEFT);
            for (int i = 0; i < 5; i++)
                sbText(c, heads[i], cols[i], y + 124, 19, legend, Paint.Align.CENTER);
            float top = y + 136;
            for (int i = 0; i < rows.length; i++) {
                SbRow r = rows[i];
                float ry = top + i * row, lean = i % 2 != 0 ? -2 : 2;
                if (r.local) {
                    float[] band = {x + 26, ry + 3 + lean, x + w - 28, ry + 3 - lean,
                            x + w - 30, ry + row - 3 - lean, x + 24, ry + row - 3 + lean};
                    quad(c, band, 0xff4a1a1e, false, 0);
                    quad(c, band, edge, true, 1.5f);
                } else {
                    m_Card.setStyle(Paint.Style.FILL);
                    m_Card.setColor(0x66827763);
                    c.drawRect(x + 34, ry + row - 1, x + w - 34, ry + row, m_Card);
                }
                float base = ry + row * 0.5f + 10;
                int color = colors[r.character & 3];
                quad(c, new float[]{x + 40, ry + 15, x + 49, ry + 13, x + 48, ry + row - 15, x + 41, ry + row - 13}, color, false, 0);
                sbText(c, r.name, x + 58, base, 29, color, Paint.Align.LEFT);
                sbValue(c, r.score, cols[0], base);
                sbValue(c, r.kills, cols[1], base);
                sbValue(c, r.downs, cols[2], base);
                sbValue(c, r.revives, cols[3], base);
                sbValue(c, r.headshots, cols[4], base);
            }
            TimerSnapshot t = m_Timer;
            if (t != null)
                sbText(c, "MATCH TIME  " + t.elapsed, x + w - 56, y + h - 16, 20, legend, Paint.Align.RIGHT);
        }

        @Override
        protected void onDraw(Canvas c) {
            boolean waitBanner=s_WaitingPlayer && m_Cursor.screenNow()==null;
            SbRow[] sbRows = m_SbRows;
            boolean sbOn = s_SbHeld && s_SbAvailable && !waitBanner && sbRows.length > 0;
            int fps = s_Fps;
            boolean fpsOn = s_FpsShown && fps > 0;
            if (!m_Show && !m_ShowTimer && !m_MainTotal && !m_ProfileTotal && !waitBanner && !fpsOn && !sbOn)
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
            if (waitBanner) {
                int waitSave = c.save();
                c.scale(s, s);
                drawCoopPause(c);
                c.restoreToCount(waitSave);
                postInvalidateDelayed(400);   // pontinhos
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
            if (sbOn) {
                int sbSave = c.save();
                c.scale(s, s);
                drawScoreboard(c, sbRows);
                c.restoreToCount(sbSave);
            }
            if (fpsOn) {   // igual ao do PC: "FPS N", 22 px, amarelo, centralizado no rodapé
                int fpsSave = c.save();
                c.scale(s, s);
                if (m_FpsPaint == null) {
                    m_FpsPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
                    m_FpsPaint.setTypeface(m_Comic);
                    m_FpsPaint.setTextSize(22);
                    m_FpsPaint.setTextAlign(Paint.Align.CENTER);
                    m_FpsPaint.setColor(0xFFF5C518);
                    m_FpsPaint.setShadowLayer(2f, 1f, 1f, 0xC0000000);
                }
                c.drawText("FPS " + fps, 640, 712, m_FpsPaint);
                c.restoreToCount(fpsSave);
            }
            c.restoreToCount(save);
        }
        private Paint m_FpsPaint;
    }
}
