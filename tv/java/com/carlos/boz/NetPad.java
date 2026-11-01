package com.carlos.boz;

import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.KeyEvent;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * Controle ligado no PC, repassado pela rede (tools/pc-pad.py). Pacote UDP na porta 27050, little endian:
 *   "BOZP" | u8 versão(1) | u8 0 | u16 seq | u32 botões | i16 lx ly rx ry lt rt | i8 hatX hatY | 16 bytes nome
 * Bits dos botões: 0 A, 1 B, 2 X, 3 Y, 4 L1, 5 R1, 6 L2, 7 R2, 8 SELECT, 9 START, 10 MODE, 11 L3, 12 R3.
 * Entra pelo mesmo caminho de um controle Bluetooth (teclas traduzidas + analógicos do Pad).
 */
final class NetPad {
    static final int PORT = 27050;
    private static final String TAG = "BozPad";
    private static final long TIMEOUT_MS = 1500;
    private static final int DEVICE_ID = -27050;   // id fictício de dispositivo
    private static final int[] KEYS = {
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_BUTTON_Y,
            KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_BUTTON_L2, KeyEvent.KEYCODE_BUTTON_R2,
            KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_BUTTON_MODE,
            KeyEvent.KEYCODE_BUTTON_THUMBL, KeyEvent.KEYCODE_BUTTON_THUMBR,
    };

    private final Pad m_Pad;
    private final Handler m_Handler = new Handler(Looper.getMainLooper());
    private volatile long m_LastPacket;
    private boolean m_Active;
    private int m_Buttons;
    private String m_Name = "";

    NetPad(Pad pad) {
        m_Pad = pad;
        Thread t = new Thread(this::listen, "BozNetPad");
        t.setDaemon(true);
        t.start();
        m_Handler.postDelayed(m_Watchdog, TIMEOUT_MS);
    }

    boolean isActive() {
        return m_Active;
    }

    String name() {
        return m_Name;
    }

    private void listen() {
        try (DatagramSocket sock = new DatagramSocket(PORT)) {
            Log.i(TAG, "NetPad ouvindo UDP " + PORT);
            byte[] buf = new byte[64];
            DatagramPacket pkt = new DatagramPacket(buf, buf.length);
            while (true) {
                sock.receive(pkt);
                if (pkt.getLength() < 42 || buf[0] != 'B' || buf[1] != 'O' || buf[2] != 'Z' || buf[3] != 'P' || buf[4] != 1)
                    continue;
                ByteBuffer b = ByteBuffer.wrap(buf, 0, pkt.getLength()).order(ByteOrder.LITTLE_ENDIAN);
                final int buttons = b.getInt(8);
                final float[] ax = new float[6];
                for (int i = 0; i < 6; i++)
                    ax[i] = b.getShort(12 + i * 2) / 32767f;
                final int hx = buf[24], hy = buf[25];
                final String name = new String(buf, 26, 16, "US-ASCII").trim();
                m_LastPacket = SystemClock.uptimeMillis();
                m_Handler.post(() -> apply(buttons, ax, hx, hy, name));
            }
        } catch (Exception e) {
            Log.e(TAG, "NetPad", e);
        }
    }

    private void apply(int buttons, float[] ax, int hx, int hy, String name) {
        if (!m_Active) {
            m_Active = true;
            m_Name = name;
            Log.i(TAG, "NetPad conectado: " + name);
            m_Pad.onNetPadChanged();
        }
        int changed = buttons ^ m_Buttons;
        for (int i = 0; i < KEYS.length; i++) {
            if ((changed & (1 << i)) != 0)
                m_Pad.key((buttons & (1 << i)) != 0 ? KeyEvent.ACTION_DOWN : KeyEvent.ACTION_UP, KEYS[i], DEVICE_ID);
        }
        m_Buttons = buttons;
        m_Pad.applyAxes(dead(ax[0]), dead(ax[1]), dead(ax[2]), dead(ax[3]), ax[4], ax[5], hx, hy, DEVICE_ID);
    }

    private static float dead(float v) {
        return Math.abs(v) > 0.18f ? v : 0f;
    }

    /** Sem pacotes por um tempo: solta tudo e avisa que o controle saiu. */
    private final Runnable m_Watchdog = new Runnable() {
        @Override
        public void run() {
            if (m_Active && SystemClock.uptimeMillis() - m_LastPacket > TIMEOUT_MS) {
                apply(0, new float[6], 0, 0, m_Name);
                m_Active = false;
                Log.i(TAG, "NetPad desconectado");
                m_Pad.onNetPadChanged();
            }
            m_Handler.postDelayed(this, 500);
        }
    };
}
