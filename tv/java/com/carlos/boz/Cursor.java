package com.carlos.boz;

import android.app.Activity;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.TextView;

/**
 * Os menus do jogo só aceitam toque (o Xperia Play também tinha tela de toque).
 * No modo menu, nas telas mapeadas (Menus) o direcional move um foco entre os botões e A toca o botão em foco;
 * nas demais, uma seta é movida pelos analógicos/direcional e A toca onde ela estiver.
 * SELECT alterna entre modo menu e modo jogo.
 */
final class Cursor {
    private static final float SPEED = 900f;        // pixels por segundo com o analógico no máximo
    private static final float DPAD_SPEED = 600f;
    private static final long MIN_TAP_MS = 120;      // o jogo lê o dedo por frame: toque curto demais é ignorado

    private final Activity m_Activity;
    private final Handler m_Handler = new Handler(Looper.getMainLooper());
    private final ArrowView m_View;
    private final TextView m_Toast;

    private boolean m_MenuMode = true;
    private boolean m_PausedFromGame;   // modo menu aberto pelo "voltar" durante a partida
    private boolean m_ReturningToFrontend;
    private float m_X, m_Y;
    private float m_StickX, m_StickY;
    private int m_DpadX, m_DpadY;
    private long m_LastFrame;
    private boolean m_Ticking;
    private long m_TouchDownTime = -1;
    private long m_LastTap = -1;
    private final Menus m_Menus;
    private final NameEditor m_Names;
    private int m_StickDir = -1;        // direção atual do analógico no modo foco (-1 = centro)
    private long m_StickRepeatAt;
    private static final long FIRST_REPEAT_MS = 350, REPEAT_MS = 160;

    Cursor(Activity activity) {
        m_Activity = activity;
        m_Names = new NameEditor(activity);
        m_View = new ArrowView(activity);
        activity.addContentView(m_View, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        m_Toast = new TextView(activity);
        m_Toast.setTextColor(Color.WHITE);
        m_Toast.setTextSize(16);
        m_Toast.setBackgroundColor(0xC0000000);
        m_Toast.setPadding(24, 12, 24, 12);
        m_Toast.setVisibility(View.GONE);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        lp.topMargin = 24;
        activity.addContentView(m_Toast, lp);
        m_Menus = new Menus(activity, this::onScreen);
        showMode();
    }

    boolean isMenuMode() {
        return m_MenuMode;
    }

    /** Tela reconhecida agora (null = gameplay, carregamento ou tela sem mapa). */
    String screenNow() {
        return m_Menus.screenId();
    }

    /** Menu de fora da partida (menu principal, seleção de mapas...) ou Game Over: a partida acabou. */
    static boolean outsideMatch(String id) {
        return id != null && (!INGAME.contains(id) || "gameover".equals(id));
    }

    /** O carregamento mede ~8 s no Kino; 15 s cobre o toque que o disparou. */
    boolean tappedRecently(long withinMs) {
        return m_LastTap >= 0 && SystemClock.uptimeMillis() - m_LastTap < withinMs;
    }

    void onLoadingFinished() {
        if (!m_ReturningToFrontend && m_Menus.screenId() == null && tappedRecently(15000))
            setMenuMode(false);
    }

    void setMenuMode(boolean menu) {
        m_PausedFromGame = false;
        if (menu == m_MenuMode)
            return;
        m_MenuMode = menu;
        if (!menu) { cancelPreparation(); finishSelectionEntry(); m_Names.close(false); endTransition(); }
        if (!menu)
            m_LastScreen = null;   // a próxima tela de menu reconhecida (mesmo repetida) religa o modo menu
        releaseAll();
        showMode();
        if (!menu) Pad.onGameMode();
    }

    void bringToFront() {
        m_View.bringToFront();
        m_Toast.bringToFront();
    }

    /** Teclas antes da tradução para o Xperia. true = consumida aqui. */
    boolean onKey(KeyEvent e) {
        int code = e.getKeyCode();
        if (m_Names.isOpen()) {             // editor de nome: tela só do Android, nada chega ao jogo
            m_Names.onKey(code, e.getAction() == KeyEvent.ACTION_DOWN, e.getRepeatCount());
            m_View.invalidate();
            return true;
        }
        // Só o apertar é bloqueado: o soltar precisa chegar, senão o toque fica preso e o SWF ignora os seguintes.
        boolean press = e.getAction() == KeyEvent.ACTION_DOWN;
        if (press && m_Trans && (code == KeyEvent.KEYCODE_BUTTON_A || code == KeyEvent.KEYCODE_DPAD_CENTER
                || code == KeyEvent.KEYCODE_BUTTON_B
                || (code >= KeyEvent.KEYCODE_DPAD_UP && code <= KeyEvent.KEYCODE_DPAD_RIGHT)))
            return true;                    // trocando de tela: a tela antiga ainda está no SWF
        if (press && SystemClock.uptimeMillis() < m_SettleUntil && (code == KeyEvent.KEYCODE_BUTTON_A
                || code == KeyEvent.KEYCODE_DPAD_CENTER || code == KeyEvent.KEYCODE_BUTTON_B))
            return true;                    // tela recém-chegada: o SWF ainda anima a entrada e ignora toques
        if (m_SelectionEntry && (code == KeyEvent.KEYCODE_BUTTON_A
                || code == KeyEvent.KEYCODE_DPAD_CENTER || code == KeyEvent.KEYCODE_BUTTON_B
                || code == KeyEvent.KEYCODE_BUTTON_START
                || (code >= KeyEvent.KEYCODE_DPAD_UP && code <= KeyEvent.KEYCODE_DPAD_RIGHT)))
            return true;
        boolean down = e.getAction() == KeyEvent.ACTION_DOWN;
        if (code == KeyEvent.KEYCODE_BUTTON_SELECT) {
            if (down && e.getRepeatCount() == 0)
                setMenuMode(!m_MenuMode);
            return true;
        }
        // START segue para o jogo (pausa do Xperia Play); o cursor acompanha a pausa
        if (code == KeyEvent.KEYCODE_BUTTON_START && down && e.getRepeatCount() == 0) {
            if (!m_MenuMode) {
                setMenuMode(true);
                m_PausedFromGame = true;
                // Dead Ops ignora a tecla de pause (o analógico de tiro mantém o touchpad ativo): toca o botão
                // de pause do HUD (canto superior direito)
                if (Hud.s_DeadOps) tapAt(1228, 48);
            } else if (m_PausedFromGame || "pause".equals(m_Menus.screenId())) {
                setMenuMode(false);
            }
            return false;
        }
        if (!m_MenuMode)
            return false;
        String sid = m_Menus.screenId();
        if (press && e.getRepeatCount() == 0 && sid != null && sid.startsWith("title")
                && (code == KeyEvent.KEYCODE_BUTTON_A || code == KeyEvent.KEYCODE_DPAD_CENTER))
            beginTransition(sid, "main", null);   // o menu principal aparece já no aperto, antes do SWF sair do título
        if (m_Menus.active() && onFocusKey(code, down, e.getRepeatCount()))
            return true;
        switch (code) {
            case KeyEvent.KEYCODE_BUTTON_A:
            case KeyEvent.KEYCODE_DPAD_CENTER:
                if (e.getRepeatCount() == 0) {
                    if (down) touchDown(); else touchUp();
                }
                return true;
            case KeyEvent.KEYCODE_DPAD_LEFT:  m_DpadX = down ? -1 : (m_DpadX < 0 ? 0 : m_DpadX); tick(); return true;
            case KeyEvent.KEYCODE_DPAD_RIGHT: m_DpadX = down ? 1 : (m_DpadX > 0 ? 0 : m_DpadX); tick(); return true;
            case KeyEvent.KEYCODE_DPAD_UP:    m_DpadY = down ? -1 : (m_DpadY < 0 ? 0 : m_DpadY); tick(); return true;
            case KeyEvent.KEYCODE_DPAD_DOWN:  m_DpadY = down ? 1 : (m_DpadY > 0 ? 0 : m_DpadY); tick(); return true;
        }
        return false;
    }

    // menus da partida (todos mapeados): saindo deles para uma tela desconhecida = voltou ao gameplay
    private static final java.util.Set<String> INGAME = new java.util.HashSet<>(java.util.Arrays.asList(
            "pause", "pausesettings", "sensitivity", "savequit", "quitconfirm", "gameover"));
    private String m_LastScreen;
    private final Runnable m_BackToGame = this::backToGame;

    private void backToGame() {
        if (!m_ReturningToFrontend && m_MenuMode && m_Menus.screenId() == null) setMenuMode(false);
    }

    /**
     * Toda tela de menu reconhecida durante o modo jogo (pausa, Game Over, voltou ao menu principal) liga o
     * modo menu. Volta ao modo jogo: RETURN, START na pausa, fim de um carregamento de mapa, ou 1 s numa
     * tela desconhecida logo depois de um menu da partida.
     */
    private void onScreen() {
        String id = m_Menus.screenId();
        if (outsideMatch(id)) m_ReturningToFrontend = false;
        if (m_Names.isOpen() && id != null && !"main".equals(id)) m_Names.close(false);
        if (m_Trans && id != null && !id.equals(m_TransFrom)) endTransition();
        if (id != null && !id.equals(m_LastKnownScreen)) m_SettleUntil = SystemClock.uptimeMillis() + SETTLE_MS;
        // Sonda falhou no meio de uma animação do SWF: segura a cortina em vez de mostrar o menu original.
        // Saindo do título (o A vai direto ao jogo, sem foco), a próxima tela é o menu principal: já aparece.
        if (id == null && !m_Trans && m_MenuMode && m_LastKnownScreen != null && !INGAME.contains(m_LastKnownScreen)
                && !MAP_SCREEN.equals(m_LastKnownScreen))
            beginTransition(m_LastKnownScreen, m_LastKnownScreen.startsWith("title") ? "main" : "*", null);
        if (m_SelectionEntry && m_EntrySent && id != null && !id.equals(m_EntryOrigin))
            finishSelectionEntry(); // charselect, or the existing new-game confirmation dialog
        if (m_SliderEdit && (id == null || !id.equals(m_LastScreen)))
            endEdit();
        if (id != null && !id.equals(m_LastScreen) && !m_MenuMode)
            setMenuMode(true);
        // a arte do carrossel muda com os toques e a tela pisca como desconhecida: só conta entrada vinda de outra tela conhecida
        if (MAP_SCREEN.equals(id) && !MAP_SCREEN.equals(m_LastKnownScreen)) {
            m_CarouselAt = -1;
            m_MapBusy = true;
            m_Handler.postDelayed(m_EnterSelection, 700);
        }
        if (!MAP_SCREEN.equals(id) && MAP_SCREEN.equals(m_LastKnownScreen)) {
            cancelPreparation();
            m_LastKnownScreen = null;
        }
        if (id != null) m_LastKnownScreen = id;
        m_Handler.removeCallbacks(m_BackToGame);
        if (id == null && m_LastScreen != null && INGAME.contains(m_LastScreen) && m_MenuMode)
            m_Handler.postDelayed(m_BackToGame, 1000);
        if (id != null || m_LastScreen == null || !INGAME.contains(m_LastScreen))
            m_LastScreen = id;
        m_View.invalidate();
    }

    /** Tela reconhecida: o direcional move o foco, A toca o botão em foco; sliders são arrastados. */
    private boolean onFocusKey(int code, boolean down, int repeat) {
        int dir;
        switch (code) {
            case KeyEvent.KEYCODE_DPAD_LEFT: dir = Menus.LEFT; break;
            case KeyEvent.KEYCODE_DPAD_RIGHT: dir = Menus.RIGHT; break;
            case KeyEvent.KEYCODE_DPAD_UP: dir = Menus.UP; break;
            case KeyEvent.KEYCODE_DPAD_DOWN: dir = Menus.DOWN; break;
            case KeyEvent.KEYCODE_BUTTON_B:
                if (m_SliderEdit) {          // B solta o slider em vez de voltar de tela
                    if (down && repeat == 0) endEdit();
                    return true;
                }
                if (MAP_SCREEN.equals(m_Menus.screenId())) {
                    if (down && repeat == 0) {
                        cancelPreparation();
                        tapAt(267, 622);
                    }
                    return true;
                }
                if (down && repeat == 0) beginTransition(m_Menus.screenId(), m_View.m_Styled.back(m_Menus.screenId()), null);
                return false;
            case KeyEvent.KEYCODE_BUTTON_A:
            case KeyEvent.KEYCODE_DPAD_CENTER:
                if (focusOnSlider()) {       // A agarra/solta o slider
                    if (down && repeat == 0) {
                        if (m_SliderEdit) endEdit(); else { m_SliderEdit = true; m_View.invalidate(); }
                    }
                    return true;
                }
                if (repeat == 0) {
                    Menus.Item it = m_Menus.focused();
                    if (it != null && it.timerToggle) {
                        if (down) { Hud.toggleTimer(); m_View.invalidate(); }
                        return true;
                    }
                    if (it != null && it.nameEditor) {
                        if (down) { m_Names.open(); m_View.invalidate(); }
                        return true;
                    }
                    if (it != null && it.selection) {
                        if (down) beginSelectionEntry(it);
                        return true;
                    }
                    if (it != null && it.character >= 0) {
                        if (down && !m_Launching && m_PickedMap != 2) {
                            m_PickedCharacter = it.character;
                            if (!m_MapBusy && !m_MapFailed) applyCharacter();
                            m_View.invalidate();
                        }
                        return true;
                    }
                    if (it != null && it.start) {
                        if (down) startSelectedGame();
                        return true;
                    }
                    if (it != null && it.map >= 0) {
                        if (down) pickMap(it.map);
                        return true;
                    }
                    if (down && it != null && !it.slider) {
                        if ("savequit".equals(m_Menus.screenId()) && it.tx > 640) {
                            m_ReturningToFrontend = true;
                            m_Handler.removeCallbacks(m_BackToGame);
                            android.util.Log.i("BozPad", "Save & Quit: mantendo controle no frontend");
                            Hud.onSaveQuit();
                        }
                        if (MAP_SCREEN.equals(m_Menus.screenId())) cancelPreparation();
                        // CONTINUE (botão da direita da tela Single): a partida volta com o tempo salvo
                        if ("single".equals(m_Menus.screenId()) && it.x > 600 && !it.selection) Hud.onContinue();
                        if (!it.stay && !it.toGame) beginTransition(m_Menus.screenId(), it.to, it);
                        m_X = it.tx * sx();
                        m_Y = it.ty * sy();
                        touchDown();
                    } else if (!down) {
                        touchUp();
                        if (it != null && it.toGame)
                            m_Handler.postDelayed(() -> setMenuMode(false), MIN_TAP_MS + 100);
                    }
                }
                return true;
            default:
                return false;
        }
        if (m_SliderEdit) {
            // slider agarrado: segurar ao longo do trilho = arrastar; direção perpendicular solta e navega
            if (down && repeat == 0) slideStart(dir);
            else if (!down && dir == m_SlideDir) slideStop();
            return true;
        }
        if (down)
            focusMove(dir);
        return true;
    }

    // Transição entre telas: ao tocar um botão que troca de tela, o overlay não espera o SWF. Se o destino é uma
    // página Java, ela aparece na hora; senão a cortina (moldura parada) cobre o SWF até a sonda ver a tela nova.
    private static final long TRANS_SAME_MS = 900;   // a tela de origem continua: o toque não mudou nada
    private static final long TRANS_MAX_MS = 2500;   // tela sem mapa (About, carregamento): mostra o SWF
    private static final long SETTLE_MS = 350;       // tela nova: A/B esperam o SWF terminar de entrar
    private boolean m_Trans, m_TransRetried, m_TransSnap;
    private String m_TransFrom, m_TransTo;
    private Menus.Item m_TransItem;                  // botão tocado: repete o toque uma vez se o SWF o ignorou
    private long m_TransAt, m_SettleUntil;
    private final Runnable m_TransCheck = this::checkTransition;

    /** to: tela de destino, "*" (destino desconhecido) ou null (não navega). */
    private void beginTransition(String from, String to, Menus.Item item) {
        if (to == null || from == null) return;
        m_Trans = true;
        m_TransSnap = from.startsWith("title");   // o título é do SWF: sem fade, para o menu original não aparecer
        m_TransItem = item;
        m_TransRetried = false;
        m_TransFrom = from;
        m_TransTo = "*".equals(to) ? null : to;
        m_TransAt = SystemClock.uptimeMillis();
        m_Handler.removeCallbacks(m_TransCheck);
        m_Handler.postDelayed(m_TransCheck, TRANS_SAME_MS);
        m_Handler.postDelayed(m_TransCheck, TRANS_MAX_MS);
        m_View.invalidate();
    }

    private void checkTransition() {
        if (!m_Trans) return;
        long t = SystemClock.uptimeMillis() - m_TransAt;
        String id = m_Menus.screenId();
        if (t < TRANS_MAX_MS && m_TransFrom.equals(id) && m_TransItem != null && !m_TransRetried) {
            m_TransRetried = true;           // o SWF ainda animava e perdeu o toque: tenta de novo
            android.util.Log.i("BozPad", "transição: repetindo toque em " + id);
            tapAt(m_TransItem.tx, m_TransItem.ty);
            m_Handler.postDelayed(m_TransCheck, TRANS_SAME_MS);
            return;
        }
        if (t >= TRANS_MAX_MS || (t >= TRANS_SAME_MS && m_TransFrom.equals(id))) {
            android.util.Log.i("BozPad", "transição sem tela nova (" + id + "), liberando");
            endTransition();
        }
    }

    private void endTransition() {
        if (!m_Trans) return;
        m_Trans = false;
        m_Handler.removeCallbacks(m_TransCheck);
        m_View.invalidate();
    }

    // Cover the SurfaceView before sending the touch that opens the original SWF.
    // PixelCopy still sees the engine surface, so recognition works under this cover.
    private boolean m_SelectionEntry, m_EntryQueued, m_EntrySent;
    private String m_EntryOrigin;
    private int m_EntryGeneration;
    private Menus.Item m_EntryItem;
    private final Runnable m_EntryTimeout = this::selectionEntryTimeout;
    private void selectionEntryTimeout() {
        android.util.Log.w("BozPad", "selection entry timeout: " + m_Menus.screenId());
        finishSelectionEntry();
    }
    private final Runnable m_SendEntry = () -> {
        if (!m_SelectionEntry || m_EntrySent || !m_MenuMode) return;
        m_EntrySent = true;
        android.util.Log.i("BozPad", "selection cover committed; sending entry touch");
        tapAt(m_EntryItem.tx, m_EntryItem.ty);
    };

    private void beginSelectionEntry(Menus.Item item) {
        releaseAll();
        m_EntryGeneration++;
        m_EntryOrigin = m_Menus.screenId();
        m_EntryItem = item;
        android.util.Log.i("BozPad", "selection entry requested from " + m_EntryOrigin);
        m_SelectionEntry = true;
        m_EntryQueued = m_EntrySent = false;
        m_Handler.postDelayed(m_EntryTimeout, 4000);
        m_View.invalidate();
    }

    private void finishSelectionEntry() {
        m_EntryGeneration++;
        m_Handler.removeCallbacks(m_EntryTimeout);
        m_View.removeCallbacks(m_SendEntry);
        m_SelectionEntry = m_EntryQueued = m_EntrySent = false;
        m_View.invalidate();
    }

    private boolean focusOnSlider() {
        Menus.Item it = m_Menus.focused();
        return it != null && it.slider;
    }

    /** Toque rápido num ponto da tela (coordenadas 1280x720), sem mover a seta. */
    void tapAt(float x, float y) {
        if (m_TouchDownTime >= 0) return;
        float ox = m_X, oy = m_Y;
        m_X = x * sx(); m_Y = y * sy();
        android.util.Log.i("BozPad", "toque em " + Math.round(m_X) + "," + Math.round(m_Y));
        touchDown();
        m_Handler.postDelayed(() -> { touchUp(); m_X = ox; m_Y = oy; }, MIN_TAP_MS + 30);
    }

    // ---- lista de mapas (tela de personagem): o app desenha a lista e opera as setas do carrossel do jogo
    private static final String MAP_SCREEN = "charselect";
    /** OBB atual: tutorial=0 (oculto na lista), kino=1, deadops=2, ascension=3, cotd=4. */
    static final String[] MAP_NAMES = {"KINO DER TOTEN", "DEAD OPS ARCADE", "ASCENSION", "CALL OF THE DEAD"};
    static final int[] MAP_INDEX = {1, 2, 3, 4};
    private static final float[] LEFT_ARROW = {447, 490}, RIGHT_ARROW = {845, 495};
    // cada toque só conta quando a arte do centro muda; o jogo perde toques enquanto carrega a arte (Dead Ops)
    private static final long ARROW_POLL_MS = 150;
    private static final long ARROW_SETTLE_MS = 450;    // depois da troca, espera a animação antes do próximo toque
    private static final long ARROW_TIMEOUT_MS = 2500;  // sem troca: perdeu o toque (ou chegou na ponta)
    private static final float ART_CHANGED = 12f;       // artDiff acima disso = outra arte
    private int m_PickedMap = 1;        // índice no carrossel
    private int m_PickedCharacter;
    private boolean m_MapBusy, m_MapFailed, m_Launching;
    private long m_MapDeadline;
    private final Runnable m_EnterSelection = () -> pickMap(m_PickedMap);
    private final Runnable m_ApplyCharacter = this::applyCharacter;
    private final Runnable m_StartSelection = () -> {
        if (selectionVisible() && !m_MapBusy && !m_MapFailed) tapAt(980, 630);
    };
    private final Runnable m_ResetLaunch = this::resetLaunch;

    private void resetLaunch() { m_Launching = false; m_View.invalidate(); }

    private boolean selectionVisible() {
        return m_MenuMode && MAP_SCREEN.equals(m_Menus.screenId());
    }

    private void cancelPreparation() {
        m_Handler.removeCallbacks(m_EnterSelection);
        m_Handler.removeCallbacks(m_ArrowStep);
        m_Handler.removeCallbacks(m_ApplyCharacter);
        m_Handler.removeCallbacks(m_StartSelection);
        m_Handler.removeCallbacks(m_ResetLaunch);
        m_TapAt = -1;
        m_MapDeadline = 0;
        m_MapBusy = false;
        m_Launching = false;
    }

    private void applyCharacter() {
        if (!selectionVisible() || m_MapBusy || m_MapFailed || m_PickedMap == 2) return;
        // Centros dos quatro botões originais; a arte própria nunca recebe esses toques.
        float[] ys = {156, 263, 380, 496};
        tapAt(297, ys[m_PickedCharacter]);
        android.util.Log.i("BozPad", "personagem " + CharacterMenu.CHARACTERS[m_PickedCharacter]);
    }

    private void startSelectedGame() {
        if (!selectionVisible() || m_MapBusy || m_MapFailed || m_Launching || m_TouchDownTime >= 0) return;
        m_Launching = true;
        m_Handler.removeCallbacks(m_ApplyCharacter);
        applyCharacter();
        // Espera o ACTION_UP do personagem antes de tocar o botão original de início.
        m_Handler.postDelayed(m_StartSelection, 300);
        m_Handler.postDelayed(m_ResetLaunch, 1800);
        m_View.invalidate();
    }
    private int m_CarouselAt = -1;      // -1 = desconhecido (volta tudo para a esquerda antes)
    private String m_LastKnownScreen;   // última tela reconhecida (ignora as piscadas de "desconhecida")
    private int[] m_ArtBefore;          // arte antes do toque pendente
    private long m_TapAt = -1;          // hora do toque pendente (-1 = nenhum)
    private int m_TapDir;               // -1 esquerda, +1 direita

    private void pickMap(int index) {
        if (!selectionVisible() || m_Launching) return;
        m_PickedMap = index;
        if (m_MapFailed) m_CarouselAt = -1;
        if (!m_MapBusy || m_MapDeadline == 0) m_MapDeadline = SystemClock.uptimeMillis() + 20000;
        m_MapFailed = false;
        m_MapBusy = true;
        m_Handler.removeCallbacks(m_ApplyCharacter);
        android.util.Log.i("BozPad", "mapa " + index + " (carrossel em " + m_CarouselAt + ")");
        m_Handler.removeCallbacks(m_ArrowStep);
        m_Handler.post(m_ArrowStep);
        m_View.invalidate();
    }

    private void arrowTap(int dir) {
        m_ArtBefore = m_Menus.artSignature();
        m_TapDir = dir;
        m_TapAt = SystemClock.uptimeMillis();
        float[] p = dir < 0 ? LEFT_ARROW : RIGHT_ARROW;
        tapAt(p[0], p[1]);
    }

    private final Runnable m_ArrowStep = new Runnable() {
        @Override
        public void run() {
            if (!selectionVisible()) { cancelPreparation(); return; }
            long now = SystemClock.uptimeMillis();
            if (now > m_MapDeadline) {
                m_TapAt = -1;
                m_MapBusy = false;
                m_MapFailed = true;
                m_View.invalidate();
                android.util.Log.w("BozPad", "mapa: tempo esgotado; início bloqueado até nova seleção");
                return;
            }
            if (m_TapAt >= 0) {
                if (Menus.artDiff(m_ArtBefore, m_Menus.artSignature()) > ART_CHANGED) {
                    if (m_CarouselAt >= 0) m_CarouselAt += m_TapDir;
                    m_TapAt = -1;
                    m_Handler.postDelayed(this, ARROW_SETTLE_MS);
                    return;
                }
                if (now - m_TapAt < ARROW_TIMEOUT_MS) { m_Handler.postDelayed(this, ARROW_POLL_MS); return; }
                m_TapAt = -1;
                if (m_CarouselAt < 0 && m_TapDir < 0) {
                    m_CarouselAt = 0;   // OBB instalado ainda tem Tutorial na extremidade esquerda.
                    android.util.Log.i("BozPad", "carrossel no começo");
                } else {
                    android.util.Log.i("BozPad", "toque perdido, repetindo");
                    arrowTap(m_TapDir);
                    m_Handler.postDelayed(this, ARROW_POLL_MS);
                    return;
                }
            }
            if (m_CarouselAt < 0) arrowTap(-1);
            else if (m_CarouselAt != m_PickedMap) arrowTap(m_PickedMap > m_CarouselAt ? 1 : -1);
            else {
                android.util.Log.i("BozPad", "carrossel no mapa " + m_CarouselAt);
                m_MapBusy = false;
                m_Handler.postDelayed(m_ApplyCharacter, 200);
                m_View.invalidate();
                return;
            }
            m_Handler.postDelayed(this, ARROW_POLL_MS);
        }
    };

    private void focusMove(int dir) {
        if (m_Names.isOpen()) m_Names.move(dir); else m_Menus.move(dir);
        m_View.invalidate();
    }

    // ---- arraste contínuo de slider
    private static final float SLIDE_SPEED = 250f;   // px/s (em 1280x720)
    private int m_SlideDir = -1;
    private float m_SlideEX, m_SlideEY;
    private long m_SlideLast;

    private boolean m_SliderEdit;

    private void endEdit() {
        if (m_SlideDir >= 0) slideStop();
        m_SliderEdit = false;
        m_View.invalidate();
    }

    private void slideStart(int dir) {
        if (m_SlideDir >= 0) slideStop();
        float[] d = m_Menus.move(dir, true);   // {botãoX, botãoY, fimX, fimY} ou null se a direção sai do slider
        if (d == null) { m_SliderEdit = false; m_View.invalidate(); return; }
        m_SlideDir = dir;
        m_X = d[0] * sx(); m_Y = d[1] * sy();
        m_SlideEX = d[2] * sx(); m_SlideEY = d[3] * sy();
        touchDown();
        m_SlideLast = SystemClock.uptimeMillis() + 120;   // segura parado um instante para o jogo pegar o botão
        m_Handler.postDelayed(m_Slide, 120);
    }

    private final Runnable m_Slide = new Runnable() {
        @Override
        public void run() {
            if (m_SlideDir < 0) return;
            long now = SystemClock.uptimeMillis();
            float step = SLIDE_SPEED * sx() * (now - m_SlideLast) / 1000f;
            m_SlideLast = now;
            float dx = m_SlideEX - m_X, dy = m_SlideEY - m_Y, len = (float) Math.hypot(dx, dy);
            if (len > 0.5f) {
                float k = Math.min(1f, step / len);
                moveTo(m_X + dx * k, m_Y + dy * k);
            }
            m_Handler.postDelayed(this, 16);
        }
    };

    private void slideStop() {
        m_SlideDir = -1;
        m_Handler.removeCallbacks(m_Slide);
        touchUp();
    }

    private float sx() { return m_View.getWidth() / 1280f; }
    private float sy() { return m_View.getHeight() / 720f; }

    /** Qualquer um dos dois analógicos move a seta (o que estiver mais inclinado), ou o foco na tela conhecida. */
    void setSticks(float lx, float ly, float rx, float ry) {
        if (m_SelectionEntry) return;
        boolean right = rx * rx + ry * ry > lx * lx + ly * ly;
        float x = right ? rx : lx, y = right ? ry : ly;
        if (m_Menus.active()) {
            m_StickX = m_StickY = 0f;
            int dir = -1;
            if (Math.max(Math.abs(x), Math.abs(y)) > 0.6f)
                dir = Math.abs(x) > Math.abs(y) ? (x < 0 ? Menus.LEFT : Menus.RIGHT) : (y < 0 ? Menus.UP : Menus.DOWN);
            long now = SystemClock.uptimeMillis();
            if (m_SliderEdit) {
                if (dir != m_StickDir) {
                    m_StickDir = dir;
                    if (m_SlideDir >= 0) slideStop();
                    if (dir >= 0) slideStart(dir);
                }
                return;
            }
            if (dir != m_StickDir) {
                m_StickDir = dir;
                if (dir >= 0) { focusMove(dir); m_StickRepeatAt = now + FIRST_REPEAT_MS; }
            } else if (dir >= 0 && now >= m_StickRepeatAt) {
                focusMove(dir);
                m_StickRepeatAt = now + REPEAT_MS;
            }
            return;
        }
        m_StickX = x;
        m_StickY = y;
        tick();
    }

    private void tick() {
        if (m_Ticking || !moving())
            return;
        m_Ticking = true;
        m_LastFrame = SystemClock.uptimeMillis();
        m_Handler.post(m_Frame);
    }

    private boolean moving() {
        return m_StickX != 0f || m_StickY != 0f || m_DpadX != 0 || m_DpadY != 0;
    }

    private final Runnable m_Frame = new Runnable() {
        @Override
        public void run() {
            if (!m_MenuMode || !moving()) {
                m_Ticking = false;
                return;
            }
            long now = SystemClock.uptimeMillis();
            float dt = Math.min(0.05f, (now - m_LastFrame) / 1000f);
            m_LastFrame = now;
            float vx = m_StickX * SPEED + m_DpadX * DPAD_SPEED;
            float vy = m_StickY * SPEED + m_DpadY * DPAD_SPEED;
            moveTo(m_X + vx * dt, m_Y + vy * dt);
            m_Handler.postDelayed(this, 16);
        }
    };

    private void moveTo(float x, float y) {
        int w = m_View.getWidth(), h = m_View.getHeight();
        m_X = Math.max(0, Math.min(w - 1, x));
        m_Y = Math.max(0, Math.min(h - 1, y));
        if (m_TouchDownTime >= 0)
            sendTouch(MotionEvent.ACTION_MOVE);
        m_View.invalidate();
    }

    private void touchDown() {
        if (m_TouchDownTime >= 0)
            return;
        m_TouchDownTime = SystemClock.uptimeMillis();
        m_LastTap = m_TouchDownTime;
        sendTouch(MotionEvent.ACTION_DOWN);
        m_View.setPressed(true);
        m_View.invalidate();
    }

    private void touchUp() {
        if (m_TouchDownTime < 0)
            return;
        long held = SystemClock.uptimeMillis() - m_TouchDownTime;
        if (held < MIN_TAP_MS) {
            m_Handler.postDelayed(this::touchUp, MIN_TAP_MS - held);
            return;
        }
        sendTouch(MotionEvent.ACTION_UP);
        m_TouchDownTime = -1;
        m_View.setPressed(false);
        m_View.invalidate();
    }

    private void sendTouch(int action) {
        long now = SystemClock.uptimeMillis();
        MotionEvent ev = MotionEvent.obtain(m_TouchDownTime, now, action, m_X, m_Y, 0);
        ev.setSource(InputDevice.SOURCE_TOUCHSCREEN);
        m_Activity.dispatchTouchEvent(ev);
        ev.recycle();
    }

    private void releaseAll() {
        m_StickX = m_StickY = 0f;
        m_DpadX = m_DpadY = 0;
        if (m_TouchDownTime >= 0) {
            sendTouch(MotionEvent.ACTION_UP);
            m_TouchDownTime = -1;
        }
    }

    private final Runnable m_HideToast = this::hideToast;

    private void hideToast() {
        m_Toast.setVisibility(View.GONE);
    }

    private void showMode() {
        m_View.setVisibility(m_MenuMode ? View.VISIBLE : View.GONE);
        m_Menus.setFocusMode(m_MenuMode);
        m_Toast.setText(m_MenuMode
                ? "Menu: D-pad to navigate, A to select, B to go back  •  SELECT = game mode"
                : "Game mode  •  SELECT = menu mode (pointer)");
        m_Toast.setVisibility(View.VISIBLE);
        m_Toast.bringToFront();
        m_Handler.removeCallbacks(m_HideToast);
        m_Handler.postDelayed(m_HideToast, 3000);
    }

    /** Seta desenhada por cima do jogo; não recebe toques. */
    private final class ArrowView extends View {
        private final Paint m_Fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint m_Stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path m_Arrow = new Path();
        private final Paint m_Focus = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint m_Shadow = new Paint(Paint.ANTI_ALIAS_FLAG);

        ArrowView(Context c) {
            super(c);
            setClickable(false);
            setFocusable(false);
            m_Stroke.setStyle(Paint.Style.STROKE);
            m_Stroke.setStrokeWidth(3);
            m_Stroke.setColor(Color.BLACK);
            m_Focus.setStyle(Paint.Style.STROKE);
            m_Focus.setStrokeWidth(6);
            m_Shadow.setStyle(Paint.Style.STROKE);
            m_Shadow.setStrokeWidth(12);
            m_Shadow.setColor(0x99000000);
            float s = 1.6f;
            m_Arrow.moveTo(0, 0);
            m_Arrow.lineTo(0, 22 * s);
            m_Arrow.lineTo(6 * s, 17 * s);
            m_Arrow.lineTo(10 * s, 26 * s);
            m_Arrow.lineTo(14 * s, 24 * s);
            m_Arrow.lineTo(10 * s, 15.5f * s);
            m_Arrow.lineTo(17 * s, 15.5f * s);
            m_Arrow.close();
        }

        @Override
        protected void onSizeChanged(int w, int h, int ow, int oh) {
            if (ow == 0) {
                m_X = w / 2f;
                m_Y = h / 2f;
            }
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            return false;
        }

        private final CharacterMenu m_CharacterMenu = new CharacterMenu(m_Activity);
        private final BrandScreen m_BrandScreen = new BrandScreen(m_Activity);
        private final MainMenu m_MainMenu = new MainMenu(m_Activity);
        private final StyledScreen m_Styled = new StyledScreen(m_Activity);

        // Referência selecao/B_cantoneiras.png: quatro cantos brancos com sombra preta,
        // sem escurecer a tela. O foco desliza entre os itens.
        private final Path m_Corners = new Path();
        private static final float GLIDE = 0.35f;       // fração do caminho andada por quadro
        private final RectF m_Spot = new RectF();
        private final Paint m_Ink = new Paint(Paint.ANTI_ALIAS_FLAG);
        private boolean m_SpotValid;

        private void drawSpotlight(Canvas c, Menus.Item it) {
            drawSpotlight(c, it.x, it.y, it.x + it.w, it.y + it.h);
        }

        /** Retângulo em 1280x720. */
        private void drawSpotlight(Canvas c, float x0, float y0, float x1, float y1) {
            float sx = sx(), sy = sy();
            float l = x0 * sx, t = y0 * sy, r = x1 * sx, b = y1 * sy;
            if (!m_SpotValid) {
                m_Spot.set(l, t, r, b);
                m_SpotValid = true;
            } else {
                m_Spot.left += (l - m_Spot.left) * GLIDE;
                m_Spot.top += (t - m_Spot.top) * GLIDE;
                m_Spot.right += (r - m_Spot.right) * GLIDE;
                m_Spot.bottom += (b - m_Spot.bottom) * GLIDE;
                if (Math.abs(l - m_Spot.left) + Math.abs(t - m_Spot.top) + Math.abs(r - m_Spot.right)
                        + Math.abs(b - m_Spot.bottom) > 2)
                    postInvalidateOnAnimation();
                else
                    m_Spot.set(l, t, r, b);
            }
            float corner = Math.min(34 * sx, Math.min(m_Spot.width(), m_Spot.height()) * 0.32f);
            m_Corners.reset();
            m_Corners.moveTo(m_Spot.left, m_Spot.top + corner);
            m_Corners.lineTo(m_Spot.left, m_Spot.top);
            m_Corners.lineTo(m_Spot.left + corner, m_Spot.top);
            m_Corners.moveTo(m_Spot.right - corner, m_Spot.top);
            m_Corners.lineTo(m_Spot.right, m_Spot.top);
            m_Corners.lineTo(m_Spot.right, m_Spot.top + corner);
            m_Corners.moveTo(m_Spot.left, m_Spot.bottom - corner);
            m_Corners.lineTo(m_Spot.left, m_Spot.bottom);
            m_Corners.lineTo(m_Spot.left + corner, m_Spot.bottom);
            m_Corners.moveTo(m_Spot.right - corner, m_Spot.bottom);
            m_Corners.lineTo(m_Spot.right, m_Spot.bottom);
            m_Corners.lineTo(m_Spot.right, m_Spot.bottom - corner);
            m_Ink.setStyle(Paint.Style.STROKE);
            m_Ink.setStrokeJoin(Paint.Join.MITER);
            m_Ink.setStrokeCap(Paint.Cap.SQUARE);
            m_Ink.setColor(0xFF000000);
            m_Ink.setStrokeWidth(9 * sx);
            c.drawPath(m_Corners, m_Ink);
            m_Ink.setColor(0xFFFFFFFF);
            m_Ink.setStrokeWidth(5 * sx);
            c.drawPath(m_Corners, m_Ink);
        }

        // Troca de tela: a moldura (cortina) fica parada e o conteúdo se dissolve de uma página para a outra.
        private static final float FADE_MS = 160f, COVER_MS = 200f;
        private String m_ShownId, m_FromId;
        private long m_ShownAt, m_LastFrame;
        private float m_Cover;

        /** Tela que o overlay mostra: o destino previsto da transição, a cortina (null) ou a tela reconhecida. */
        private String displayId() {
            if (m_Trans) return fullPage(m_TransTo) ? m_TransTo : null;
            return m_Menus.screenId();
        }

        private boolean fullPage(String id) {
            return "main".equals(id) || MAP_SCREEN.equals(id) || m_Styled.isPage(id);
        }

        private void drawFull(Canvas c, String id) {
            if ("main".equals(id)) m_MainMenu.draw(c, getWidth(), getHeight());
            else if (MAP_SCREEN.equals(id)) m_CharacterMenu.draw(c, getWidth(), getHeight(),
                    m_PickedMap, m_PickedCharacter, m_MapBusy, m_MapFailed, m_Launching);
            else m_Styled.draw(c, getWidth(), getHeight(), id);
        }

        private void layer(Canvas c, float alpha) {
            c.saveLayerAlpha(0, 0, getWidth(), getHeight(), Math.round(255 * Math.max(0, Math.min(1, alpha))));
        }

        /** Cortina e páginas Java. true = a tela inteira é do overlay (nada do SWF aparece). */
        private boolean drawPages(Canvas c) {
            long now = SystemClock.uptimeMillis();
            long gap = now - m_LastFrame;
            float dt = gap > 50 ? 16 : gap;   // primeiro quadro depois de parado: um passo normal
            m_LastFrame = now;
            String shown = m_MenuMode ? displayId() : null;
            if (!java.util.Objects.equals(shown, m_ShownId)) {
                m_FromId = m_ShownId;
                m_ShownId = shown;
                m_ShownAt = now;
            }
            boolean curtain = m_MenuMode && m_Trans && shown == null, full = fullPage(shown);
            if (m_TransSnap && m_Trans) {
                m_TransSnap = false;
                m_Cover = 1;
                m_ShownAt = now - (long) FADE_MS;
                m_FromId = null;
            }
            float a = Math.min(1f, (now - m_ShownAt) / FADE_MS);
            float target = curtain || full ? 1 : 0;
            m_Cover = target > m_Cover ? Math.min(target, m_Cover + dt / COVER_MS) : Math.max(target, m_Cover - dt / COVER_MS);
            boolean fromFull = a < 1 && fullPage(m_FromId);
            if (m_Cover > 0) {
                layer(c, m_Cover);
                // as janelas da página (números do SWF) ficam abertas também na cortina
                if (full) m_Styled.clipWindows(c, shown, getWidth(), getHeight());
                m_Styled.drawCurtain(c, getWidth(), getHeight(), m_Trans ? now - m_TransAt : 0);
                c.restore();
            }
            if (fromFull) { layer(c, 1 - a); drawFull(c, m_FromId); c.restore(); }
            if (full) {
                if (a < 1) layer(c, a);
                drawFull(c, shown);
                if (a < 1) c.restore();
            }
            if (a < 1 || m_Cover != target || curtain) postInvalidateOnAnimation();
            if (!full && !curtain) return false;
            Menus.Item selected = m_Trans ? null : m_Menus.focused();
            if (selected != null) drawSpotlight(c, selected); else m_SpotValid = false;
            return true;
        }

        @Override
        protected void onDraw(Canvas c) {
            if (m_SelectionEntry) {
                c.drawColor(Color.BLACK);
                m_CharacterMenu.draw(c, getWidth(), getHeight(),
                        m_PickedMap, m_PickedCharacter, true, false, false);
                if (!m_EntryQueued) {
                    m_EntryQueued = true;
                    final int generation = m_EntryGeneration;
                    Runnable committed = () -> m_Handler.post(() -> {
                        if (generation == m_EntryGeneration) m_SendEntry.run();
                    });
                    if (android.os.Build.VERSION.SDK_INT >= 29 && isHardwareAccelerated()) {
                        getViewTreeObserver().registerFrameCommitCallback(committed);
                        // Registration during onDraw may be captured on the next traversal.
                        postInvalidateOnAnimation();
                    } else
                        postOnAnimation(() -> postOnAnimation(committed));
                }
                return;
            }
            if (m_Names.isOpen()) {
                m_Names.draw(c, getWidth(), getHeight());
                RectF k = m_Names.focus();
                drawSpotlight(c, k.left, k.top, k.right, k.bottom);
                return;
            }
            if (!m_Trans && ("title".equals(m_Menus.screenId()) || "title_prompt".equals(m_Menus.screenId()))) {
                m_BrandScreen.drawTitle(c, getWidth(), getHeight());
                return; // sem seta/cantoneiras sobre o único botão do título
            }
            if (drawPages(c)) return;
            if (m_Styled.draw(c, getWidth(), getHeight(), m_Menus.screenId())) {
                Menus.Item selected = m_Menus.focused();
                if (selected != null) drawSpotlight(c, selected);
                return;
            }
            Menus.Item it = m_Menus.focused();
            if (it != null) {
                drawSpotlight(c, it);
                return;
            }
            m_SpotValid = false;
            c.save();
            c.translate(m_X, m_Y);
            m_Fill.setColor(isPressed() ? 0xFF6FE0B9 : Color.WHITE);
            c.drawPath(m_Arrow, m_Fill);
            c.drawPath(m_Arrow, m_Stroke);
            c.restore();
        }
    }
}
