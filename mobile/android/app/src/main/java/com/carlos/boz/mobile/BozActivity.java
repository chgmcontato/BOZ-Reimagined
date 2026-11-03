package com.carlos.boz.mobile;

import android.content.Context;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;

/**
 * The runtime: SDL's activity running libmain.so (src/ built by mobile/native) with the
 * arguments SetupActivity chose. The PC's main() runs unchanged as SDL_main.
 */
public final class BozActivity extends SDLActivity {
    static final String EXTRA_ARGS = "com.carlos.boz.mobile.ARGS";
    private static final String TAG = "BozRuntime";

    private WifiManager.MulticastLock m_Multicast;

    /** platform_android.c: start this run's log file. */
    private static native void nativeSetLogFile(String path);

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }

    @Override
    protected String[] getArguments() {
        String[] args = getIntent().getStringArrayExtra(EXTRA_ARGS);
        return args != null ? args : new String[0];
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state); // loads libSDL2.so and libmain.so
        try {
            nativeSetLogFile(new File(getExternalFilesDir(null), "boz-runtime.log").getPath());
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "libmain.so did not load", e);
        }
        // Without the lock Android drops incoming multicast: the Local Wi-Fi lobby (mDNS) needs it.
        WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        if (wifi != null) {
            m_Multicast = wifi.createMulticastLock("boz-coop-mdns");
            m_Multicast.setReferenceCounted(false);
            m_Multicast.acquire();
            Log.i(TAG, "multicast lock held");
        }
    }

    @Override
    protected void onDestroy() {
        if (m_Multicast != null && m_Multicast.isHeld()) {
            m_Multicast.release();
        }
        boolean finishing = isFinishing();
        super.onDestroy();
        if (finishing) {
            // The next PLAY gets a fresh process: guest memory, JIT and SDL start from zero.
            Log.i(TAG, "game process ends");
            android.os.Process.killProcess(android.os.Process.myPid());
        }
    }
}
