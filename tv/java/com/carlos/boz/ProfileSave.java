package com.carlos.boz;

import android.content.Context;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

/**
 * Saves do Marmalade em files/data-gles1 (.i3d: versão, Adler-32 com o campo zerado, dados; o jogo ainda confere
 * um 2º checksum guardado à parte). Aqui: reset único do perfil e o arquivo de nome lido pelo patch nativo.
 */
final class ProfileSave {
    private static final String TAG = "BozSave";
    private static final String RESET_KEY = "profileReset1";
    private static final String[] RESET_FILES = {
        "profile_0.i3d", "save_stats_0.i3d", "1_save_game.i3d", "2_save_game.i3d",
        "save_achievement.i3d", "mm.i3d", "bostik.i3d", "rcpt.i3d"};

    private ProfileSave() {}

    private static File dir(Context c) { return new File(c.getFilesDir(), "data-gles1"); }

    /** Executa uma vez por instalação: renomeia saves, estatísticas e COD Points para .bak (perfil novo). */
    static void resetOnce(Context c) {
        if (c.getSharedPreferences("boz_tv", Context.MODE_PRIVATE).getBoolean(RESET_KEY, false)) return;
        File d = dir(c);
        for (String n : RESET_FILES) moveAside(new File(d, n));
        moveAside(new File(c.getFilesDir(), "__ng__.dat"));
        File[] rc = c.getFilesDir().listFiles((f, n) -> n.endsWith(".rcpt"));
        if (rc != null) for (File f : rc) moveAside(f);
        c.getSharedPreferences("boz_tv", Context.MODE_PRIVATE).edit().putBoolean(RESET_KEY, true).commit();
    }

    private static void moveAside(File f) {
        if (!f.exists()) return;
        File bak = new File(f.getPath() + ".bak");
        bak.delete();
        Log.i(TAG, "reset " + f + (f.renameTo(bak) ? " -> .bak" : " FALHOU"));
    }

    /**
     * Grava o nome do P1 em data-gles1/boz_name.txt, que o código nativo lê (patch_s3e.py, readname) para o perfil
     * e o co-op. Não escreve o profile_0.i3d: o jogo confere um 2º checksum guardado à parte e apagaria o arquivo.
     */
    static void writeName(Context c, String name) {
        File d = dir(c);
        d.mkdirs();
        try (FileOutputStream out = new FileOutputStream(new File(d, "boz_name.txt"))) {
            out.write(name.toUpperCase().getBytes(StandardCharsets.US_ASCII));
            Log.i(TAG, "nome " + name);
        } catch (Exception e) { Log.e(TAG, "boz_name.txt", e); }
    }
}
