package com.carlos.boz;

import android.util.Log;

import java.io.BufferedReader;
import java.io.FileReader;
import java.lang.reflect.Method;

/**
 * Leitura da memória do jogo (código Marmalade boz.s3e), sem lib nativa: o /proc/self/mem é barrado pelo
 * SELinux da TV, então lê direto com libcore.io.Memory (peek*) do próprio processo, só em regiões que o
 * /proc/self/maps diz estarem mapeadas.
 * A imagem é achada por uma assinatura de 16 bytes no código (mesma do frida/run.py).
 */
final class GameMemory {
    private static final String TAG = "BozPad";
    private static final byte[] SIG = {
            0x2d, (byte) 0xe9, (byte) 0xf0, 0x43, 0x1c, 0x46, 0x66, 0x4b,
            (byte) 0xc1, (byte) 0xb0, 0x07, 0x46, (byte) 0x89, 0x46, 0x7b, 0x44};
    private static final long SIG_OFFSET = 0x1d4ae0;
    // na tela de resultados o jogo grava STAT_WAVES = *(*(base + 0x4958c4) + 0x78)
    private static final long ROUND_GLOBAL = 0x4958c4, ROUND_FIELD = 0x78;

    private static long s_Base;
    private static Method s_PeekInt, s_PeekBytes;

    /** Rodada atual da partida, ou -1 se não der para ler. */
    static int round() {
        try {
            if (s_PeekInt == null) {
                Class<?> m = Class.forName("libcore.io.Memory");
                s_PeekInt = m.getDeclaredMethod("peekInt", long.class, boolean.class);
                s_PeekBytes = m.getDeclaredMethod("peekByteArray", long.class, byte[].class, int.class, int.class);
            }
            if (s_Base == 0) s_Base = findBase();
            if (s_Base == 0) return -1;
            long obj = readU32(s_Base + ROUND_GLOBAL);
            if (obj == 0 || !mapped(obj + ROUND_FIELD)) return -1;
            long v = readU32(obj + ROUND_FIELD);
            return v > 0 && v < 1000 ? (int) v : -1;
        } catch (Exception e) {
            Log.e(TAG, "GameMemory", e);
            return -1;
        }
    }

    private static long readU32(long addr) throws Exception {
        return ((Integer) s_PeekInt.invoke(null, addr, false)) & 0xffffffffL;
    }

    /** O endereço está numa região legível? (ler fora dela derrubaria o jogo) */
    private static boolean mapped(long addr) throws Exception {
        try (BufferedReader r = new BufferedReader(new FileReader("/proc/self/maps"))) {
            String line;
            while ((line = r.readLine()) != null) {
                String[] f = line.trim().split("\\s+");
                String[] range = f[0].split("-");
                long start = Long.parseLong(range[0], 16), end = Long.parseLong(range[1], 16);
                if (addr >= start && addr + 4 <= end) return f[1].startsWith("r");
            }
        }
        return false;
    }

    /** Procura a assinatura nas regiões executáveis anônimas (onde o Marmalade carrega o s3e). */
    private static long findBase() throws Exception {
        try (BufferedReader r = new BufferedReader(new FileReader("/proc/self/maps"))) {
            String line;
            while ((line = r.readLine()) != null) {
                String[] f = line.trim().split("\\s+");
                if (f.length < 2 || !f[1].startsWith("r") || f[1].charAt(2) != 'x') continue;
                if (f.length >= 6 && f[5].startsWith("/")) continue;   // só anônimas
                String[] range = f[0].split("-");
                long start = Long.parseLong(range[0], 16), end = Long.parseLong(range[1], 16);
                if (end - start < 0x100000 || end - start > 0x2000000) continue;
                byte[] buf = new byte[(int) (end - start)];
                s_PeekBytes.invoke(null, start, buf, 0, buf.length);
                int hit = indexOf(buf, SIG);
                if (hit >= 0) {
                    long base = start + hit - SIG_OFFSET;
                    Log.i(TAG, "GameMemory: imagem do jogo em 0x" + Long.toHexString(base));
                    return base;
                }
            }
        }
        Log.w(TAG, "GameMemory: imagem do jogo não encontrada");
        return 0;
    }

    private static int indexOf(byte[] buf, byte[] pat) {
        outer:
        for (int i = 0; i <= buf.length - pat.length; i++) {
            if (buf[i] != pat[0]) continue;
            for (int k = 1; k < pat.length; k++) if (buf[i + k] != pat[k]) continue outer;
            return i;
        }
        return -1;
    }
}
