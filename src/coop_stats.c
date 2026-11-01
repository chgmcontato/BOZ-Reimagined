#include "coop_stats.h"
#include "host.h"
#include "zeroconf_platform.h"

#ifdef _WIN32
typedef int coop_socklen;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
typedef socklen_t coop_socklen;
#endif

enum { PORT = 27051, PACKET = 28, MAX_PEERS = 8, MAX_PLAYERS = 8, FRESH_MS = 5000, SEND_MS = 1000 };

static struct {
    uint32_t hash;
    uint64_t heard;
    struct coop_stats stats;
} g_players[MAX_PLAYERS];
static struct {
    struct in_addr address;
    uint64_t heard;
} g_peers[MAX_PEERS];
static int g_socket = -1;

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool open_socket(void) {
    static bool failed;
    if (g_socket >= 0 || failed) {
        return g_socket >= 0;
    }
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    int fd = (int)socket(AF_INET, SOCK_DGRAM, 0), on = 1;
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(PORT);
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof(on));
#ifdef _WIN32
    u_long nonblocking = 1;
    bool ready = fd >= 0 && ioctlsocket((SOCKET)fd, FIONBIO, &nonblocking) == 0;
#else
    bool ready = fd >= 0 && fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) == 0;
#endif
    if (!ready || bind(fd, (struct sockaddr *)&local, sizeof(local)) != 0) {
        log_info("co-op stats: UDP %d unavailable, others' stats stay hidden", PORT);
        failed = true;
        return false;
    }
    g_socket = fd;
    log_info("co-op stats: listening on UDP %d", PORT);
    return true;
}

static void remember_peer(struct in_addr address, uint64_t now) {
    int slot = 0;
    for (int i = 0; i < MAX_PEERS; ++i) {
        if (g_peers[i].address.s_addr == address.s_addr) {
            slot = i;
            break;
        }
        if (g_peers[i].heard < g_peers[slot].heard) {
            slot = i;
        }
    }
    g_peers[slot].address = address;
    g_peers[slot].heard = now;
}

static void receive_all(uint32_t mine, uint64_t now) {
    uint8_t p[64];
    for (int n = 0; n < 32; ++n) {
        struct sockaddr_in from;
        coop_socklen size = sizeof(from);
        int got = (int)recvfrom(g_socket, (char *)p, sizeof(p), 0, (struct sockaddr *)&from, &size);
        if (got < 0) {
            return;
        }
        uint32_t hash = got == PACKET ? get32(p + 8) : mine;
        if (got != PACKET || memcmp(p, "BOZS", 4) != 0 || get32(p + 4) != 1 || hash == mine) {
            continue;
        }
        remember_peer(from.sin_addr, now);
        int slot = 0;
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            if (g_players[i].hash == hash) {
                slot = i;
                break;
            }
            if (g_players[i].heard < g_players[slot].heard) {
                slot = i;
            }
        }
        g_players[slot].hash = hash;
        g_players[slot].heard = now;
        g_players[slot].stats.kills = (int)get32(p + 12);
        g_players[slot].stats.downs = (int)get32(p + 16);
        g_players[slot].stats.revives = (int)get32(p + 20);
        g_players[slot].stats.headshots = (int)get32(p + 24);
    }
}

static void send_to(const uint8_t *p, struct in_addr address) {
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_addr = address;
    to.sin_port = htons(PORT);
    sendto(g_socket, (const char *)p, PACKET, 0, (const struct sockaddr *)&to, sizeof(to));
}

void coop_stats_tick(uint32_t name_hash, const struct coop_stats *mine) {
    static uint64_t last;
    if (!open_socket()) {
        return;
    }
    uint64_t now = monotonic_ms();
    receive_all(name_hash, now);
    if (now - last < SEND_MS) {
        return;
    }
    last = now;
    uint8_t p[PACKET];
    memcpy(p, "BOZS", 4);
    put32(p + 4, 1);
    put32(p + 8, name_hash);
    put32(p + 12, (uint32_t)mine->kills);
    put32(p + 16, (uint32_t)mine->downs);
    put32(p + 20, (uint32_t)mine->revives);
    put32(p + 24, (uint32_t)mine->headshots);
    struct in_addr addresses[16], masks[16];
    int count = zeroconf_platform_list_ipv4(addresses, masks, 16);
    for (int i = 0; i < count; ++i) {
        struct in_addr broadcast;
        broadcast.s_addr = addresses[i].s_addr | ~masks[i].s_addr;
        send_to(p, broadcast);
    }
    for (int i = 0; i < MAX_PEERS; ++i) {
        if (g_peers[i].heard && now - g_peers[i].heard < FRESH_MS) {
            send_to(p, g_peers[i].address);
        }
    }
}

bool coop_stats_lookup(uint32_t name_hash, struct coop_stats *out) {
    uint64_t now = monotonic_ms();
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (g_players[i].heard && g_players[i].hash == name_hash && now - g_players[i].heard < FRESH_MS) {
            *out = g_players[i].stats;
            return true;
        }
    }
    return false;
}
