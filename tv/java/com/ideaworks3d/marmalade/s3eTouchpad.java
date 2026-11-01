package com.ideaworks3d.marmalade;

import android.view.MotionEvent;

/**
 * Reimplementação da extensão s3eTouchpad do Marmalade (touchpads analógicos do Xperia Play),
 * que foi removida deste APK. A interface foi extraída do libs3e_android.so:
 *   <init>()V, s3eTouchpadInit()Z, s3eTouchpadTerminate()V, s3eTouchpadGetInt(I)I
 *   native onMotionEvent(IIII)V  -> (touchId, ação, x, y); ação 5 = solta, 6 = move, outra = toca
 * Aqui o "touchpad" é alimentado pelos analógicos do controle (ver com.carlos.boz.Pad).
 */
public class s3eTouchpad {
    public static final int WIDTH = 966;   // tamanho real do touchpad do Xperia Play
    public static final int HEIGHT = 360;

    public static final int ACTION_DOWN = 0;
    public static final int ACTION_UP = 5;
    public static final int ACTION_MOVE = 6;

    private static s3eTouchpad s_Instance;
    private boolean m_Running;

    public s3eTouchpad() {
        s_Instance = this;
        android.util.Log.i("BozPad", "s3eTouchpad criado pelo motor");
    }

    public boolean s3eTouchpadInit() {
        m_Running = true;
        android.util.Log.i("BozPad", "s3eTouchpadInit chamado pelo jogo");
        return true;
    }

    public void s3eTouchpadTerminate() {
        m_Running = false;
    }

    private static final long LOADING_GAP_MS = 3000;
    private static long s_LastPoll;

    /** Chamado a cada frame pelo jogo. Uma pausa longa entre chamadas = tela de carregamento. */
    public int s3eTouchpadGetInt(int prop) {
        long now = android.os.SystemClock.uptimeMillis();
        if (s_LastPoll != 0 && now - s_LastPoll > LOADING_GAP_MS)
            com.carlos.boz.Pad.onLoadingFinished();
        s_LastPoll = now;
        switch (prop) {
            case 0: return 1;       // S3E_TOUCHPAD_AVAILABLE
            case 1: return WIDTH;   // S3E_TOUCHPAD_WIDTH
            case 2: return HEIGHT;  // S3E_TOUCHPAD_HEIGHT
            default: return 0;
        }
    }

    public native void onMotionEvent(int touchId, int action, int x, int y);

    /** Chamado por LoaderActivity.dispatchTouchEvent via reflexão; toques de tela seguem normais. */
    public static boolean onTouchEvent(MotionEvent event) {
        return false;
    }

    public static boolean send(int touchId, int action, int x, int y) {
        s3eTouchpad t = s_Instance;
        if (t == null || !t.m_Running)
            return false;
        t.onMotionEvent(touchId, action, x, y);
        return true;
    }
}
