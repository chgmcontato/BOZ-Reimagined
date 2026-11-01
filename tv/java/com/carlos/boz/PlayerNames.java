package com.carlos.boz;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * Nomes dos jogadores locais (P1 e P2, este reservado para o co-op), fora do save do jogo.
 * O jogo original não tem campo de nome; guardar no Android não toca nos .i3d nem quebra saves antigos.
 */
final class PlayerNames {
    static final int MAX = 15;   // o co-op copia o nome num buffer de 16 bytes (patch_s3e.py, netname)
    static final int SLOTS = 2;
    private static final String PREFS = "boz_tv";   // mesmo arquivo do Pad (gamepadSeen)

    private PlayerNames() {}

    private static String key(int slot) { return "player" + (slot + 1) + "Name"; }

    static String fallback(int slot) { return "PLAYER " + (slot + 1); }

    /** Nome salvo, ou o padrão quando não há nome (instalação limpa) ou ele ficou vazio. */
    static String get(Context c, int slot) {
        String v = sanitize(c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(key(slot), null));
        return v.isEmpty() ? fallback(slot) : v;
    }

    static void set(Context c, int slot, String name) {
        String v = sanitize(name);
        SharedPreferences.Editor e = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit();
        if (v.isEmpty() || v.equals(fallback(slot))) e.remove(key(slot)); else e.putString(key(slot), v);
        e.commit();   // síncrono: o nome precisa sobreviver a fechar o jogo logo em seguida
        if (slot == 0) ProfileSave.writeName(c, get(c, 0));
    }

    /** ASCII imprimível (seguro para rede e para a fonte), sem espaços repetidos nas pontas, até MAX. */
    static String sanitize(String s) {
        if (s == null) return "";
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            if (ch < 0x20 || ch > 0x7e) continue;
            if (ch == ' ' && (b.length() == 0 || b.charAt(b.length() - 1) == ' ')) continue;
            b.append(ch);
        }
        String v = b.toString().trim();
        return v.length() > MAX ? v.substring(0, MAX).trim() : v;
    }
}
