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
import android.view.KeyEvent;
import java.io.InputStream;

/**
 * Editor de nome só pelo controle: grade de teclas no estilo das páginas de quadrinhos.
 * O jogo não lê texto (sem s3eOSReadString nem IME), então a tela é inteira do Android, aberta no menu principal.
 * D-pad move, A digita, B apaga, X espaço, Y cancela, START salva, LB/RB trocam o jogador.
 */
final class NameEditor {
    private static final int PAPER = 0xFFF2EEE4, INK = 0xFF151211, RED = 0xFF791D24, DIM = 0xDE191614;
    private static final String[] ROWS = {"1234567890", "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ-_.!"};
    private static final String[] ACTIONS = {"SPACE", "DELETE", "CANCEL", "SAVE"};
    private static final int[] SPANS = {3, 3, 2, 2};          // colunas ocupadas por tecla de ação
    private static final int COLS = 10, ACTION_ROW = ROWS.length;
    private static final float KX = 380, KY = 280, STEP_X = 82, STEP_Y = 70, KW = 78, KH = 62;

    private final Context m_Context;
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Path panel = new Path();
    private final RectF rect = new RectF();
    private final Typeface comic;
    private final Bitmap paper;
    private final StringBuilder[] m_Text = new StringBuilder[PlayerNames.SLOTS];
    private boolean m_Open;
    private int m_Slot, m_Row = 1, m_Col;

    NameEditor(Context context) {
        m_Context = context;
        Typeface loaded;
        try { loaded = Typeface.createFromAsset(context.getAssets(), "character-menu/comic.ttf"); }
        catch (Exception e) { loaded = Typeface.create("sans-serif-condensed", Typeface.BOLD_ITALIC); }
        comic = loaded;
        Bitmap b = null;
        try (InputStream in = context.getAssets().open("character-menu/paper.png")) { b = BitmapFactory.decodeStream(in); }
        catch (Exception e) { Log.e("BozPad", "NameEditor: papel ausente", e); }
        paper = b;
    }

    boolean isOpen() { return m_Open; }

    void open() {
        for (int i = 0; i < m_Text.length; i++) m_Text[i] = new StringBuilder(PlayerNames.get(m_Context, i));
        m_Slot = 0; m_Row = 1; m_Col = 0;
        m_Open = true;
    }

    /** save = grava os dois jogadores (vazio volta ao padrão); senão descarta. */
    void close(boolean save) {
        if (save) {
            for (int i = 0; i < m_Text.length; i++) PlayerNames.set(m_Context, i, m_Text[i].toString());
            Log.i("BozPad", "Nomes salvos: " + PlayerNames.get(m_Context, 0) + " / " + PlayerNames.get(m_Context, 1));
        }
        m_Open = false;
    }

    /** Teclas com o editor aberto: todas são consumidas. */
    void onKey(int code, boolean down, int repeat) {
        if (!down) return;
        switch (code) {
            case KeyEvent.KEYCODE_DPAD_LEFT: move(Menus.LEFT); break;
            case KeyEvent.KEYCODE_DPAD_RIGHT: move(Menus.RIGHT); break;
            case KeyEvent.KEYCODE_DPAD_UP: move(Menus.UP); break;
            case KeyEvent.KEYCODE_DPAD_DOWN: move(Menus.DOWN); break;
            case KeyEvent.KEYCODE_BUTTON_A:
            case KeyEvent.KEYCODE_DPAD_CENTER:
                if (repeat == 0) press();
                break;
            case KeyEvent.KEYCODE_BUTTON_B: backspace(); break;
            case KeyEvent.KEYCODE_BUTTON_X: type(' '); break;
            case KeyEvent.KEYCODE_BUTTON_Y:
            case KeyEvent.KEYCODE_BACK:
                if (repeat == 0) close(false);
                break;
            case KeyEvent.KEYCODE_BUTTON_START:
                if (repeat == 0) close(true);
                break;
            case KeyEvent.KEYCODE_BUTTON_L1:
            case KeyEvent.KEYCODE_BUTTON_R1:
                if (repeat == 0) m_Slot = (m_Slot + 1) % PlayerNames.SLOTS;
                break;
        }
    }

    void move(int dir) {
        if (dir == Menus.LEFT || dir == Menus.RIGHT) {
            int d = dir == Menus.LEFT ? -1 : 1;
            if (m_Row == ACTION_ROW) m_Col = actionCol((action(m_Col) + d + ACTIONS.length) % ACTIONS.length);
            else m_Col = (m_Col + d + COLS) % COLS;
        } else {
            int rows = ROWS.length + 1;
            m_Row = (m_Row + (dir == Menus.UP ? -1 : 1) + rows) % rows;
            if (m_Row == ACTION_ROW) m_Col = actionCol(action(m_Col));
        }
    }

    private void press() {
        if (m_Row < ACTION_ROW) { type(ROWS[m_Row].charAt(m_Col)); return; }
        switch (action(m_Col)) {
            case 0: type(' '); break;
            case 1: backspace(); break;
            case 2: close(false); break;
            default: close(true); break;
        }
    }

    private void type(char ch) {
        StringBuilder t = m_Text[m_Slot];
        if (t.length() >= PlayerNames.MAX) return;
        if (ch == ' ' && (t.length() == 0 || t.charAt(t.length() - 1) == ' ')) return;
        t.append(ch);
    }

    private void backspace() {
        StringBuilder t = m_Text[m_Slot];
        if (t.length() > 0) t.setLength(t.length() - 1);
    }

    private static int action(int col) {
        int start = 0;
        for (int i = 0; i < SPANS.length; i++) {
            if (col < start + SPANS[i]) return i;
            start += SPANS[i];
        }
        return SPANS.length - 1;
    }

    private static int actionCol(int index) {
        int start = 0;
        for (int i = 0; i < index; i++) start += SPANS[i];
        return start;
    }

    /** Tecla em foco, em 1280x720 (o Cursor desenha as cantoneiras). */
    RectF focus() {
        RectF r = new RectF();
        key(r, m_Row, m_Row == ACTION_ROW ? actionCol(action(m_Col)) : m_Col,
                m_Row == ACTION_ROW ? SPANS[action(m_Col)] : 1);
        return r;
    }

    private static void key(RectF r, int row, int col, int span) {
        float x = KX + col * STEP_X, y = KY + row * STEP_Y;
        r.set(x, y, x + span * STEP_X - (STEP_X - KW), y + KH);
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

    private void comicPanel(Canvas c, RectF r, int color, float skew) {
        panel.reset();
        panel.moveTo(r.left, r.top + skew); panel.lineTo(r.right, r.top);
        panel.lineTo(r.right - 2, r.bottom - skew); panel.lineTo(r.left + 1, r.bottom); panel.close();
        p.setStyle(Paint.Style.FILL); p.setColor(color); c.drawPath(panel, p);
        p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(4); p.setColor(INK); c.drawPath(panel, p);
        p.setStrokeWidth(1.5f); p.setColor(0xFF827763); c.drawPath(panel, p);
        p.setStyle(Paint.Style.FILL);
    }

    void draw(Canvas c, int width, int height) {
        c.save(); c.scale(width / 1280f, height / 720f);
        c.drawColor(PAPER);
        p.setColor(0xFFFFFFFF); rect.set(0, 0, 1280, 720);
        if (paper != null) c.drawBitmap(paper, null, rect, p);
        p.setTextAlign(Paint.Align.LEFT);
        p.setColor(INK); c.drawRect(48, 134, 1232, 674, p);
        c.drawRect(64, 38, 1216, 134, p);
        text(c, "BLACK OPS ZOMBIES", 86, 80, 30, PAPER, 660, false);
        text(c, "REIMAGINED", 85, 119, 42, PAPER, 660, false);
        text(c, "PLAYER NAME", 900, 100, 33, PAPER, 290, false);

        for (int i = 0; i < PlayerNames.SLOTS; i++) {
            rect.set(80, 170 + i * 110, 350, 260 + i * 110);
            comicPanel(c, rect, i == m_Slot ? RED : DIM, i % 2 == 0 ? 4 : -3);
            text(c, "PLAYER " + (i + 1) + (i == 1 ? "  (CO-OP)" : ""), 98, rect.top + 32, 20, 0xFFD5C9AC, 236, false);
            String name = m_Text[i].length() == 0 ? PlayerNames.fallback(i) : m_Text[i].toString();
            text(c, name, 98, rect.top + 72, 28, PAPER, 236, true);
        }
        text(c, "LB / RB: SWITCH PLAYER", 92, 420, 19, 0xFFD5C9AC, 250, false);
        text(c, "UP TO " + PlayerNames.MAX + " CHARACTERS", 92, 450, 19, 0xFFD5C9AC, 250, false);
        text(c, "EMPTY = DEFAULT NAME", 92, 480, 19, 0xFFD5C9AC, 250, false);

        rect.set(380, 164, 1196, 252);
        comicPanel(c, rect, PAPER, 5);
        StringBuilder t = m_Text[m_Slot];
        String shown = t.length() == 0 ? PlayerNames.fallback(m_Slot) : t.toString();
        text(c, shown + (t.length() < PlayerNames.MAX ? "_" : ""), 402, 226, 46,
                t.length() == 0 ? 0xFF9C9687 : INK, 680, false);
        p.setTextAlign(Paint.Align.RIGHT);
        text(c, t.length() + "/" + PlayerNames.MAX, 1178, 240, 20, 0xFF827763, 120, false);
        p.setTextAlign(Paint.Align.CENTER);

        RectF k = new RectF();
        for (int r = 0; r < ROWS.length; r++)
            for (int col = 0; col < COLS; col++) {
                key(k, r, col, 1);
                comicPanel(c, k, DIM, (r + col) % 2 == 0 ? 2 : -2);
                text(c, String.valueOf(ROWS[r].charAt(col)), k.centerX(), k.top + 44, 30, PAPER, KW - 8, true);
            }
        for (int i = 0; i < ACTIONS.length; i++) {
            key(k, ACTION_ROW, actionCol(i), SPANS[i]);
            comicPanel(c, k, i == ACTIONS.length - 1 ? RED : DIM, i % 2 == 0 ? 2 : -2);
            text(c, ACTIONS[i], k.centerX(), k.top + 42, 26, PAPER, k.width() - 16, true);
        }
        text(c, "D-PAD: MOVE   A: TYPE   B: DELETE   X: SPACE   Y: CANCEL   START: SAVE",
                640, 698, 20, INK, 1160, false);
        p.setTextAlign(Paint.Align.LEFT);
        c.restore();
    }
}
