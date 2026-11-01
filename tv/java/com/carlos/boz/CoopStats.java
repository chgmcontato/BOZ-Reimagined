package com.carlos.boz;

import android.os.SystemClock;
import android.util.Log;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InterfaceAddress;
import java.net.NetworkInterface;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.Map;

/** Estatísticas ao vivo no co-op, mesmo protocolo do PC (src/coop_stats.h, UDP 27051). */
final class CoopStats {
    private static final String TAG = "BozHud";
    private static final int PORT = 27051, PACKET = 28, FRESH_MS = 5000, SEND_MS = 1000;
    private final Map<Integer, int[]> m_Players = new HashMap<>();   // hash -> kills, downs, revives, headshots
    private final Map<Integer, Long> m_Heard = new HashMap<>();
    private final Map<InetAddress, Long> m_Peers = new HashMap<>();
    private DatagramSocket m_Socket;
    private boolean m_Failed;
    private long m_LastSend;
    private volatile int m_Mine;

    private boolean open() {
        if (m_Socket != null || m_Failed) return m_Socket != null;
        try {
            m_Socket = new DatagramSocket(PORT);
            m_Socket.setBroadcast(true);
            Thread t = new Thread(this::listen, "BozCoopStats");
            t.setDaemon(true);
            t.start();
            Log.i(TAG, "co-op: estatísticas em UDP " + PORT);
        } catch (Exception e) {
            m_Failed = true;
            Log.w(TAG, "co-op: UDP " + PORT + " indisponível", e);
        }
        return m_Socket != null;
    }

    private void listen() {
        byte[] buf = new byte[64];
        DatagramPacket pkt = new DatagramPacket(buf, buf.length);
        while (true) {
            try {
                m_Socket.receive(pkt);
                ByteBuffer b = ByteBuffer.wrap(buf, 0, pkt.getLength()).order(ByteOrder.LITTLE_ENDIAN);
                if (pkt.getLength() != PACKET || buf[0] != 'B' || buf[1] != 'O' || buf[2] != 'Z' || buf[3] != 'S'
                        || b.getInt(4) != 1 || b.getInt(8) == m_Mine)
                    continue;
                long now = SystemClock.uptimeMillis();
                synchronized (this) {
                    m_Peers.put(pkt.getAddress(), now);
                    m_Players.put(b.getInt(8), new int[]{b.getInt(12), b.getInt(16), b.getInt(20), b.getInt(24)});
                    m_Heard.put(b.getInt(8), now);
                }
            } catch (Exception e) {
                Log.w(TAG, "co-op: recepção", e);
                return;
            }
        }
    }

    /** Roda na thread do Hud; no máximo um envio por segundo. */
    void publish(int hash, int kills, int downs, int revives, int headshots) {
        m_Mine = hash;
        long now = SystemClock.uptimeMillis();
        if (now - m_LastSend < SEND_MS || !open()) return;
        m_LastSend = now;
        ByteBuffer b = ByteBuffer.allocate(PACKET).order(ByteOrder.LITTLE_ENDIAN);
        b.put(new byte[]{'B', 'O', 'Z', 'S'}).putInt(1).putInt(hash).putInt(kills).putInt(downs).putInt(revives).putInt(headshots);
        byte[] p = b.array();
        try {
            Enumeration<NetworkInterface> all = NetworkInterface.getNetworkInterfaces();
            while (all != null && all.hasMoreElements()) {
                NetworkInterface n = all.nextElement();
                if (!n.isUp() || n.isLoopback()) continue;
                for (InterfaceAddress a : n.getInterfaceAddresses()) {
                    InetAddress target = a.getBroadcast();
                    if (target == null && a.getAddress() instanceof java.net.Inet4Address)
                        target = broadcastOf(a);   // ZeroTier (VPN) não informa o endereço de broadcast
                    if (target != null) send(p, target);
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "co-op: interfaces", e);
        }
        InetAddress[] peers;
        synchronized (this) {
            m_Peers.values().removeIf(t -> now - t > FRESH_MS);
            peers = m_Peers.keySet().toArray(new InetAddress[0]);
        }
        for (InetAddress peer : peers) send(p, peer);
    }

    private static InetAddress broadcastOf(InterfaceAddress a) throws Exception {
        byte[] ip = a.getAddress().getAddress();
        int bits = a.getNetworkPrefixLength();
        if (bits <= 0 || bits >= 31) return null;
        for (int i = 0; i < 4; i++) {
            int keep = Math.max(0, Math.min(8, bits - i * 8));
            ip[i] |= (byte) (0xff >> keep);
        }
        return InetAddress.getByAddress(ip);
    }

    private void send(byte[] p, InetAddress target) {
        try {
            m_Socket.send(new DatagramPacket(p, p.length, target, PORT));
        } catch (Exception ignored) {
            // rede sem rota: as outras interfaces ainda podem entregar
        }
    }

    /** kills, downs, revives, headshots, ou null se o jogador não foi ouvido nos últimos 5 s. */
    synchronized int[] lookup(int hash) {
        Long t = m_Heard.get(hash);
        return t != null && SystemClock.uptimeMillis() - t < FRESH_MS ? m_Players.get(hash) : null;
    }
}
