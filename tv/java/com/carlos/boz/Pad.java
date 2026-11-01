package com.carlos.boz;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Color;
import android.hardware.input.InputManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.ideaworks3d.marmalade.s3eTouchpad;

/**
 * Faz um controle genérico se passar pelos controles do Xperia Play, que o jogo já suporta:
 *  - analógicos -> touchpads analógicos (s3eTouchpad), centros em {0.2,0.5} e {0.8,0.5}
 *  - botões     -> keycodes do Xperia Play (X = DPAD_CENTER, O = BACK, quadrado/triângulo, L/R)
 *  - gatilhos   -> L/R
 * Também mostra uma tela "conecte um controle" enquanto nenhum gamepad estiver conectado,
 * e um cursor para os menus, que só aceitam toque (ver Cursor).
 */
public final class Pad implements InputManager.InputDeviceListener {
    private static final String TAG = "BozPad";

    private static final float DEADZONE = 0.18f;
    private static final float TRIGGER_ON = 0.5f, TRIGGER_OFF = 0.3f;
    private static final int STICK_RADIUS = 150;   // em pixels do touchpad (966x360)

    private static Pad s_Pad;

    private final Activity m_Activity;
    private final InputManager m_InputManager;
    private final Handler m_Handler = new Handler(Looper.getMainLooper());
    private View m_Overlay;
    private TextView m_Status;
    private boolean m_DebugSkip;
    private Cursor m_Cursor;
    private NetPad m_Net;

    private final Stick m_Left = new Stick(0, 0.2f);
    private final Stick m_Right = new Stick(1, 0.8f);
    private boolean m_LTrigger, m_RTrigger;
    private int m_HatX, m_HatY;

    private Pad(Activity activity) {
        m_Activity = activity;
        m_InputManager = (InputManager) activity.getSystemService(Context.INPUT_SERVICE);
    }

    /** Chamado no fim de LoaderActivity.onCreate. */
    public static void attach(Activity activity) {
        try {
            System.loadLibrary("gadget");   // só existe na versão de teste (FRIDA=1)
            Log.i(TAG, "frida-gadget carregado");
        } catch (Throwable ignored) {
        }
        ProfileSave.resetOnce(activity);
        ProfileSave.writeName(activity, PlayerNames.get(activity, 0));
        s_Pad = new Pad(activity);
        s_Pad.m_Cursor = new Cursor(activity);
        new Hud(activity, s_Pad.m_Cursor, s_Pad.m_Handler);
        s_Pad.createOverlay();
        s_Pad.m_InputManager.registerInputDeviceListener(s_Pad, s_Pad.m_Handler);
        s_Pad.registerDebugReceiver();
        s_Pad.m_Net = new NetPad(s_Pad);
        s_Pad.refresh();
    }

    // ---------------------------------------------------------------- teclas

    /** Chamado por LoaderActivity.dispatchKeyEvent. Retorna o evento traduzido, ou null para engolir. */
    public static KeyEvent remapKey(KeyEvent e) {
        Pad pad = s_Pad;
        if (pad != null && pad.isWaiting())
            return null;
        int code = e.getKeyCode();
        int mapped;
        if (e.getAction() == KeyEvent.ACTION_DOWN && e.getRepeatCount() == 0) {
            boolean game = pad != null && !pad.m_Cursor.isMenuMode();
            mapped = game ? gameKey(code) : menuKey(code);
            s_DownMap.put(code, mapped);   // o "solta" usa a mesma tradução, mesmo se o modo mudar no meio
        } else {
            mapped = s_DownMap.get(code, menuKey(code));
        }
        if (mapped == code)
            return e;
        return new KeyEvent(e.getDownTime(), e.getEventTime(), e.getAction(), mapped, e.getRepeatCount(),
                e.getMetaState(), e.getDeviceId(), e.getScanCode(), e.getFlags(), e.getSource());
    }

    private static final android.util.SparseIntArray s_DownMap = new android.util.SparseIntArray();

    /**
     * Partida: layout do Black Ops 1 de console traduzido para as teclas do Xperia Play que o jogo entende
     * (DPAD_CENTER = ação/correr, BACK = recarregar, L1 = mirar, R1 = atirar, X = faca, Y = granada,
     * DPAD_DOWN = agachar, DPAD_RIGHT = trocar arma, DPAD_UP = tática, DPAD_LEFT = tiro alternativo).
     */
    private static int gameKey(int code) {
        switch (code) {
            case KeyEvent.KEYCODE_BUTTON_L2: return KeyEvent.KEYCODE_BUTTON_L1;      // LT  mirar
            case KeyEvent.KEYCODE_BUTTON_R2: return KeyEvent.KEYCODE_BUTTON_R1;      // RT  atirar
            case KeyEvent.KEYCODE_BUTTON_X: return KeyEvent.KEYCODE_DPAD_LEFT;       // X   recarregar (atalho do OBB movido p/ seta esquerda, ver patch_binds.py)
            case KeyEvent.KEYCODE_BUTTON_Y: return KeyEvent.KEYCODE_DPAD_RIGHT;      // Y   trocar arma
            case KeyEvent.KEYCODE_BUTTON_B: return KeyEvent.KEYCODE_DPAD_DOWN;       // B   agachar/deitar
            case KeyEvent.KEYCODE_BUTTON_L1: return KeyEvent.KEYCODE_DPAD_UP;        // LB  granada especial
            case KeyEvent.KEYCODE_BUTTON_R1: return KeyEvent.KEYCODE_BUTTON_Y;       // RB  granada
            case KeyEvent.KEYCODE_BUTTON_A: return KeyEvent.KEYCODE_DPAD_CENTER;     // A   ação (comprar/usar)
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return KeyEvent.KEYCODE_DPAD_CENTER; // L3 correr
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return KeyEvent.KEYCODE_BUTTON_X;   // R3  faca
            case KeyEvent.KEYCODE_BUTTON_MODE: return KeyEvent.KEYCODE_BUTTON_START;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return KeyEvent.KEYCODE_DPAD_UP;      // ->  claymore (ver dispatchKey)
            default: return code;                                                    // direcional, START
        }
    }

    /** Menus: A confirma (X do Xperia), B volta (O do Xperia). */
    private static int menuKey(int code) {
        switch (code) {
            case KeyEvent.KEYCODE_BUTTON_A: return KeyEvent.KEYCODE_DPAD_CENTER;
            case KeyEvent.KEYCODE_BUTTON_B: return KeyEvent.KEYCODE_BACK;
            case KeyEvent.KEYCODE_BUTTON_MODE: return KeyEvent.KEYCODE_BUTTON_START;
            default: return code;
        }
    }

    private static java.lang.reflect.Method s_OnKeyEvent;
    private static final android.util.SparseIntArray s_Held = new android.util.SparseIntArray();

    /**
     * Chamado por LoaderActivity.dispatchKeyEvent; true = consumido, false = segue para o sistema.
     * Este APK perdeu o onKeyDown/onKeyUp do Marmalade, então as teclas de jogo são entregues
     * direto ao LoaderActivity.onKeyEvent(keycode, pressionado, evento) privado.
     */
    public static boolean dispatchKey(Activity activity, KeyEvent original) {
        Pad pad = s_Pad;
        if (pad != null && !pad.isWaiting() && pad.m_Cursor.onKey(original))
            return true;
        KeyEvent e = remapKey(original);
        if (e == null)
            return true;
        int state = keyState(e);
        if (state < 0)
            return false;
        if (state == 2)
            return true;
        // várias fontes viram a mesma tecla (RB + RT como tecla + RT como eixo = atirar):
        // o jogo só recebe "solta" quando todas soltaram
        int target = e.getKeyCode();
        int held = s_Held.get(target);
        held = Math.max(0, held + (state == 1 ? 1 : -1));
        s_Held.put(target, held);
        if (state == 1 ? held != 1 : held != 0)
            return true;
        try {
            if (s_OnKeyEvent == null) {
                s_OnKeyEvent = Class.forName("com.ideaworks3d.marmalade.LoaderActivity")
                        .getDeclaredMethod("onKeyEvent", int.class, int.class, KeyEvent.class);
                s_OnKeyEvent.setAccessible(true);
            }
            // direcional direito = claymore: antes do "tática" (seta para cima) toca a tecla "3", cujo atalho
            // (toggleFreeMode, ver patch_binds.py) o patch_s3e.py troca por "selecionar a claymore"
            if (state == 1 && original.getKeyCode() == KeyEvent.KEYCODE_DPAD_RIGHT
                    && e.getKeyCode() == KeyEvent.KEYCODE_DPAD_UP) {
                KeyEvent three = new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_3);
                s_OnKeyEvent.invoke(activity, KeyEvent.KEYCODE_3, 1, three);
                s_OnKeyEvent.invoke(activity, KeyEvent.KEYCODE_3, 0, KeyEvent.changeAction(three, KeyEvent.ACTION_UP));
            }
            Object r = s_OnKeyEvent.invoke(activity, e.getKeyCode(), state, e);
            Log.i(TAG, "key " + KeyEvent.keyCodeToString(original.getKeyCode()) + " -> "
                    + KeyEvent.keyCodeToString(e.getKeyCode()) + " state=" + state + " native=" + r);
            // o "aim" do jogo alterna; soltar o LT toca a tecla "2", cujo atalho (alternateFire no OBB,
            // ver patch_binds.py) foi trocado por "sair da mira" (patch_s3e.py)
            if (state == 0 && target == KeyEvent.KEYCODE_BUTTON_L1
                    && original.getKeyCode() == KeyEvent.KEYCODE_BUTTON_L2) {
                KeyEvent up = new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_2);
                s_OnKeyEvent.invoke(activity, KeyEvent.KEYCODE_2, 1, up);
                s_OnKeyEvent.invoke(activity, KeyEvent.KEYCODE_2, 0, KeyEvent.changeAction(up, KeyEvent.ACTION_UP));
            }
        } catch (Exception ex) {
            Log.e(TAG, "onKeyEvent", ex);
        }
        return true;
    }

    /**
     * Como o evento (já traduzido) deve ir ao jogo: 1 = pressionou, 0 = soltou,
     * 2 = engolir (auto-repetição), -1 = não é tecla de jogo, deixa com o sistema (volume, home...).
     */
    private static int keyState(KeyEvent e) {
        switch (e.getKeyCode()) {
            case KeyEvent.KEYCODE_DPAD_UP: case KeyEvent.KEYCODE_DPAD_DOWN:
            case KeyEvent.KEYCODE_DPAD_LEFT: case KeyEvent.KEYCODE_DPAD_RIGHT:
            case KeyEvent.KEYCODE_DPAD_CENTER: case KeyEvent.KEYCODE_ENTER:
            case KeyEvent.KEYCODE_BACK: case KeyEvent.KEYCODE_MENU:
            case KeyEvent.KEYCODE_R: case KeyEvent.KEYCODE_2:
                break;
            default:
                if (!KeyEvent.isGamepadButton(e.getKeyCode()))
                    return -1;
        }
        if (e.getAction() == KeyEvent.ACTION_DOWN)
            return e.getRepeatCount() > 0 ? 2 : 1;
        if (e.getAction() == KeyEvent.ACTION_UP)
            return 0;
        return 2;
    }

    // ---------------------------------------------------------------- analógicos

    /** Chamado por LoaderActivity.onGenericMotionEvent. */
    public static boolean onMotion(MotionEvent e) {
        Pad pad = s_Pad;
        if (pad == null)
            return false;
        int src = e.getSource();
        if ((src & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK
                || e.getAction() != MotionEvent.ACTION_MOVE)
            return false;
        if (pad.isWaiting())
            return true;
        pad.handleJoystick(e);
        return true;
    }

    private String m_LastAxes = "";

    private static final int[] LOG_AXES = {MotionEvent.AXIS_X, MotionEvent.AXIS_Y, MotionEvent.AXIS_Z,
            MotionEvent.AXIS_RZ, MotionEvent.AXIS_RX, MotionEvent.AXIS_RY, MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_RTRIGGER};

    private void logAxes(MotionEvent e) {
        StringBuilder sb = new StringBuilder();
        for (int a : LOG_AXES) {
            float v = e.getAxisValue(a);
            if (Math.abs(v) > 0.3f)
                sb.append(MotionEvent.axisToString(a)).append(v > 0 ? "+ " : "- ");
        }
        String s = sb.toString();
        if (!s.equals(m_LastAxes)) {
            m_LastAxes = s;
            Log.i(TAG, "axes: " + (s.isEmpty() ? "(centro)" : s));
        }
    }

    private void handleJoystick(MotionEvent e) {
        logAxes(e);
        float lx = axis(e, MotionEvent.AXIS_X), ly = axis(e, MotionEvent.AXIS_Y);
        // padrão Android: analógico direito em Z/RZ; alguns controles usam RX/RY
        float rx = axis(e, MotionEvent.AXIS_Z), ry = axis(e, MotionEvent.AXIS_RZ);
        if (rx == 0f && ry == 0f) {
            rx = axis(e, MotionEvent.AXIS_RX);
            ry = axis(e, MotionEvent.AXIS_RY);
        }
        float lt = Math.max(e.getAxisValue(MotionEvent.AXIS_LTRIGGER), e.getAxisValue(MotionEvent.AXIS_BRAKE));
        float rt = Math.max(e.getAxisValue(MotionEvent.AXIS_RTRIGGER), e.getAxisValue(MotionEvent.AXIS_GAS));
        // D-pad reportado como HAT em vários controles genéricos
        int hx = Math.round(e.getAxisValue(MotionEvent.AXIS_HAT_X));
        int hy = Math.round(e.getAxisValue(MotionEvent.AXIS_HAT_Y));
        applyAxes(lx, ly, rx, ry, lt, rt, hx, hy, e.getDeviceId());
    }

    /** Estado dos analógicos, gatilhos e HAT (vindo do Bluetooth ou do NetPad). */
    // últimos analógicos recebidos: ao voltar do menu um analógico já inclinado não gera evento novo
    private float m_LastLx, m_LastLy, m_LastRx, m_LastRy;

    /** Modo jogo de volta (fim do pause): os "dedos" voltam já na posição atual dos analógicos. */
    static void onGameMode() {
        Pad pad = s_Pad;
        if (pad != null)
            pad.m_Handler.post(() -> {
                if (pad.m_Cursor.isMenuMode()) return;
                pad.m_Left.set(pad.m_LastLx, pad.m_LastLy);
                pad.m_Right.set(pad.m_LastRx, pad.m_LastRy);
            });
    }

    void applyAxes(float lx, float ly, float rx, float ry, float lt, float rt, int hx, int hy, int deviceId) {
        m_LastLx = lx; m_LastLy = ly; m_LastRx = rx; m_LastRy = ry;
        if (m_Cursor.isMenuMode()) {
            // modo menu: analógicos movem a seta; solta os "dedos" dos touchpads
            m_Left.set(0f, 0f);
            m_Right.set(0f, 0f);
            m_Cursor.setSticks(lx, ly, rx, ry);
        } else {
            m_Left.set(lx, ly);
            m_Right.set(rx, ry);
        }
        m_LTrigger = trigger(m_LTrigger, lt, KeyEvent.KEYCODE_BUTTON_L2, deviceId);
        m_RTrigger = trigger(m_RTrigger, rt, KeyEvent.KEYCODE_BUTTON_R2, deviceId);
        if (hx != m_HatX) {
            if (m_HatX != 0) key(KeyEvent.ACTION_UP, m_HatX < 0 ? KeyEvent.KEYCODE_DPAD_LEFT : KeyEvent.KEYCODE_DPAD_RIGHT, deviceId);
            if (hx != 0) key(KeyEvent.ACTION_DOWN, hx < 0 ? KeyEvent.KEYCODE_DPAD_LEFT : KeyEvent.KEYCODE_DPAD_RIGHT, deviceId);
            m_HatX = hx;
        }
        if (hy != m_HatY) {
            if (m_HatY != 0) key(KeyEvent.ACTION_UP, m_HatY < 0 ? KeyEvent.KEYCODE_DPAD_UP : KeyEvent.KEYCODE_DPAD_DOWN, deviceId);
            if (hy != 0) key(KeyEvent.ACTION_DOWN, hy < 0 ? KeyEvent.KEYCODE_DPAD_UP : KeyEvent.KEYCODE_DPAD_DOWN, deviceId);
            m_HatY = hy;
        }
    }

    private static float axis(MotionEvent e, int axis) {
        float v = e.getAxisValue(axis);
        return Math.abs(v) > DEADZONE ? v : 0f;
    }

    private boolean trigger(boolean down, float v, int code, int deviceId) {
        if (!down && v > TRIGGER_ON) { key(KeyEvent.ACTION_DOWN, code, deviceId); return true; }
        if (down && v < TRIGGER_OFF) { key(KeyEvent.ACTION_UP, code, deviceId); return false; }
        return down;
    }

    void key(int action, int code, int deviceId) {
        long t = android.os.SystemClock.uptimeMillis();
        m_Activity.dispatchKeyEvent(new KeyEvent(t, t, action, code, 0, 0, deviceId, 0, 0,
                InputDevice.SOURCE_GAMEPAD));
    }

    /** Um analógico do controle vira um dedo no touchpad. */
    private static final class Stick {
        final int m_Id;
        final int m_CenterX, m_CenterY;
        boolean m_Down;

        Stick(int id, float centerX) {
            m_Id = id;
            m_CenterX = Math.round(centerX * s3eTouchpad.WIDTH);
            m_CenterY = s3eTouchpad.HEIGHT / 2;
        }

        void set(float x, float y) {
            boolean active = x != 0f || y != 0f;
            if (!active) {
                if (m_Down) {
                    s3eTouchpad.send(m_Id, s3eTouchpad.ACTION_UP, m_CenterX, m_CenterY);
                    m_Down = false;
                }
                return;
            }
            int px = clamp(m_CenterX + Math.round(x * STICK_RADIUS), 0, s3eTouchpad.WIDTH - 1);
            int py = clamp(m_CenterY + Math.round(y * STICK_RADIUS), 0, s3eTouchpad.HEIGHT - 1);
            if (!m_Down) {
                // toca no centro e arrasta, como um polegar pousando no touchpad
                boolean ok = s3eTouchpad.send(m_Id, s3eTouchpad.ACTION_DOWN, m_CenterX, m_CenterY);
                Log.i(TAG, "touchpad " + m_Id + " down -> " + (ok ? "ok" : "touchpad nao inicializado pelo jogo"));
                if (!ok)
                    return;
                m_Down = true;
            }
            s3eTouchpad.send(m_Id, s3eTouchpad.ACTION_MOVE, px, py);
        }

        private static int clamp(int v, int lo, int hi) {
            return v < lo ? lo : (v > hi ? hi : v);
        }
    }

    /** Chamado pelo s3eTouchpad (thread do jogo) quando termina uma tela de carregamento. */
    public static void onLoadingFinished() {
        Pad pad = s_Pad;
        // só vale para carregamentos iniciados por um toque nos menus (ex.: "Continue", "OK");
        // o carregamento da abertura do app não tem toque antes e continua no modo menu
        if (pad != null)
            pad.m_Handler.post(() -> {
                pad.m_Cursor.onLoadingFinished();
            });
    }

    // ---------------------------------------------------------------- tela "conecte um controle"

    private boolean isWaiting() {
        return m_Overlay != null && m_Overlay.getVisibility() == View.VISIBLE;
    }

    private void createOverlay() {
        LinearLayout box = new LinearLayout(m_Activity);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setGravity(Gravity.CENTER);
        box.setBackgroundColor(0xF0101010);
        box.setClickable(true);   // bloqueia toques no jogo por baixo

        TextView title = new TextView(m_Activity);
        title.setText("Connect a controller");
        title.setTextColor(Color.WHITE);
        title.setTextSize(34);
        title.setGravity(Gravity.CENTER);
        box.addView(title);

        m_Status = new TextView(m_Activity);
        m_Status.setText("Pair a Bluetooth controller or connect a USB controller to your TV.\nThe game will continue automatically.");
        m_Status.setTextColor(0xFFBBBBBB);
        m_Status.setTextSize(18);
        m_Status.setGravity(Gravity.CENTER);
        m_Status.setPadding(0, 24, 0, 0);
        box.addView(m_Status);

        m_Overlay = box;
        m_Activity.addContentView(box, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    private void refresh() {
        String name = findGamepad();
        android.content.SharedPreferences prefs = m_Activity.getSharedPreferences("boz_tv", Context.MODE_PRIVATE);
        if (name != null)
            prefs.edit().putBoolean("gamepadSeen", true).apply();
        // o aviso só bloqueia até o primeiro controle ser visto; depois disso o jogo nunca mais espera
        boolean waiting = name == null && !m_DebugSkip && !prefs.getBoolean("gamepadSeen", false);
        Log.i(TAG, "gamepad: " + name + (m_DebugSkip ? " (debug skip)" : ""));
        m_Overlay.setVisibility(waiting ? View.VISIBLE : View.GONE);
        if (!waiting)
            m_Cursor.bringToFront();
        if (waiting) {
            m_Overlay.bringToFront();
            m_Left.set(0f, 0f);
            m_Right.set(0f, 0f);
        }
    }

    private String findGamepad() {
        if (m_Net != null && m_Net.isActive())
            return "Controle via PC (" + m_Net.name() + ")";
        for (int id : m_InputManager.getInputDeviceIds()) {
            InputDevice dev = m_InputManager.getInputDevice(id);
            if (dev == null || dev.isVirtual())
                continue;
            // só conta quem tem analógico: teclado virtual e receptor IR da TV também se dizem "gamepad"
            if ((dev.getSources() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK)
                return dev.getName();
        }
        return null;
    }

    void onNetPadChanged() { refresh(); }

    @Override public void onInputDeviceAdded(int id) { refresh(); }
    @Override public void onInputDeviceRemoved(int id) { refresh(); }
    @Override public void onInputDeviceChanged(int id) { refresh(); }

    // ---------------------------------------------------------------- depuração via adb
    // adb shell am broadcast -a com.carlos.boz.DEBUG --ez skip true
    // adb shell am broadcast -a com.carlos.boz.DEBUG --ef lx 1 --ef ly 0 --ef rx 0 --ef ry 0

    private void registerDebugReceiver() {
        m_Activity.registerReceiver(new BroadcastReceiver() {
            @Override
            public void onReceive(Context c, Intent i) {
                if (i.hasExtra("skip")) {
                    m_DebugSkip = i.getBooleanExtra("skip", false);
                    refresh();
                }
                if (i.hasExtra("lx") || i.hasExtra("ly"))
                    m_Left.set(i.getFloatExtra("lx", 0f), i.getFloatExtra("ly", 0f));
                if (i.hasExtra("rx") || i.hasExtra("ry"))
                    m_Right.set(i.getFloatExtra("rx", 0f), i.getFloatExtra("ry", 0f));
            }
        }, new IntentFilter("com.carlos.boz.DEBUG"));
    }
}
