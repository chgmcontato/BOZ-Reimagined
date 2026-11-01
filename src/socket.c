/*
 * s3eSocket / s3eInet on host sockets (adapted from cod-boz-port). The game uses
 * UDP on the LAN for Local Wi-Fi co-op; the official online servers are gone, so
 * hostname lookups fail fast instead of blocking.
 */
#include "host.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET native_socket;
#define close_native closesocket
#define last_error() WSAGetLastError()
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int native_socket;
#define close_native close
#define last_error() errno
#endif

enum {
    RESULT_SUCCESS = 0,
    RESULT_ERROR = 1,
    HANDLE_FIRST = 3000,
    SLOT_COUNT = 32,
    ERR_NONE = 0,
    ERR_PARAM = 1,
    ERR_TOO_MANY = 2,
    ERR_UNAVAIL = 5,
    ERR_UNSUPPORTED = 7,
    ERR_WOULDBLOCK = 1000,
    ERR_INPROGRESS = 1001,
    ERR_ALREADY = 1002,
    ERR_MSGSIZE = 1004,
    ERR_ADDRINUSE = 1005,
    ERR_NETDOWN = 1006,
    ERR_CONNRESET = 1007,
    ERR_ISCONN = 1008,
    ERR_NOTCONN = 1009,
    ERR_TIMEDOUT = 1011,
    ERR_CONNREFUSED = 1012,
    ERR_UNKNOWN_HOST = 1013,
    ADDR_IPV4 = 1,
    ADDR_IPV6 = 2,
    ADDR_LOCAL = 4,
    TYPE_TCP = 0,
    TYPE_UDP = 1,
};

/* s3eInetAddress as laid out by the game (0x124 bytes). */
struct inet_address {
    int32_t type;
    char local;
    char local_address[128];
    char abstract;
    uint8_t padding_0[2];
    uint32_t ip_address;
    uint8_t ipv6_address[16];
    uint16_t port;
    char string[128];
    uint8_t padding_1[2];
    uint32_t next;
};
_Static_assert(offsetof(struct inet_address, ip_address) == 0x88, "ip offset");
_Static_assert(offsetof(struct inet_address, port) == 0x9c, "port offset");
_Static_assert(sizeof(struct inet_address) == 0x124, "s3eInetAddress size");

struct slot {
    native_socket fd;
    int in_use;
    uint32_t generation;
    int32_t type;
    int connect_pending;
    uint32_t connect_cb, connect_ud;
    uint32_t accept_cb, accept_ud;
    uint32_t readable_cb, readable_ud;
    uint32_t writable_cb, writable_ud;
};

static struct slot g_slots[SLOT_COUNT];
static int32_t g_error;
static uint32_t g_lookup_cb, g_lookup_ud, g_lookup_addr; /* pending async lookup */

static int32_t map_errno(int e) {
#ifdef _WIN32
    switch (e) {
    case WSAEWOULDBLOCK:
        return ERR_WOULDBLOCK;
    case WSAEINPROGRESS:
        return ERR_INPROGRESS;
    case WSAEADDRINUSE:
        return ERR_ADDRINUSE;
    case WSAECONNRESET:
        return ERR_CONNRESET;
    case WSAECONNREFUSED:
        return ERR_CONNREFUSED;
    case WSAEMSGSIZE:
        return ERR_MSGSIZE;
    default:
        return ERR_UNAVAIL;
    }
#else
    switch (e) {
    case 0:
        return ERR_NONE;
    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
    case EWOULDBLOCK:
#endif
        return ERR_WOULDBLOCK;
    case EINPROGRESS:
        return ERR_INPROGRESS;
    case EALREADY:
        return ERR_ALREADY;
    case EINVAL:
    case EBADF:
    case EADDRNOTAVAIL:
        return ERR_PARAM;
    case EMSGSIZE:
        return ERR_MSGSIZE;
    case EADDRINUSE:
        return ERR_ADDRINUSE;
    case ENETUNREACH:
    case ENETDOWN:
    case EHOSTUNREACH:
        return ERR_NETDOWN;
    case ECONNRESET:
    case ECONNABORTED:
        return ERR_CONNRESET;
    case EISCONN:
        return ERR_ISCONN;
    case ENOTCONN:
        return ERR_NOTCONN;
    case ETIMEDOUT:
        return ERR_TIMEDOUT;
    case ECONNREFUSED:
        return ERR_CONNREFUSED;
    case EMFILE:
    case ENFILE:
        return ERR_TOO_MANY;
    default:
        return ERR_UNAVAIL;
    }
#endif
}

static uint32_t fail(void) {
    g_error = map_errno(last_error());
    return RESULT_ERROR;
}

static struct slot *slot_of(uint32_t handle) {
    if (handle < HANDLE_FIRST || handle >= HANDLE_FIRST + SLOT_COUNT ||
        !g_slots[handle - HANDLE_FIRST].in_use) {
        g_error = ERR_PARAM;
        return NULL;
    }
    return &g_slots[handle - HANDLE_FIRST];
}

static uint32_t new_slot(native_socket fd, int32_t type) {
    for (uint32_t i = 0; i < SLOT_COUNT; ++i) {
        if (!g_slots[i].in_use) {
            uint32_t generation = g_slots[i].generation + 1;
            memset(&g_slots[i], 0, sizeof(g_slots[i]));
            g_slots[i].fd = fd;
            g_slots[i].in_use = 1;
            g_slots[i].type = type;
            g_slots[i].generation = generation;
            return HANDLE_FIRST + i;
        }
    }
    close_native(fd);
    g_error = ERR_TOO_MANY;
    return 0;
}

static void set_nonblocking(native_socket fd) {
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(fd, FIONBIO, &on);
#else
    int on = 1;
    ioctl(fd, FIONBIO, &on);
    ioctl(fd, FIOCLEX);
#endif
}

static int to_sockaddr(uint32_t guest, struct sockaddr_in *out) {
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    if (!guest) {
        return 0;
    }
    const struct inet_address *a = G2H(guest);
    if (a->type & (ADDR_IPV6 | ADDR_LOCAL)) {
        g_error = ERR_UNSUPPORTED;
        return -1;
    }
    out->sin_port = a->port;
    out->sin_addr.s_addr = a->ip_address;
    return 0;
}

static void from_sockaddr(const struct sockaddr_in *in, uint32_t guest) {
    if (!guest) {
        return;
    }
    struct inet_address *a = G2H(guest);
    memset(a, 0, sizeof(*a));
    a->type = ADDR_IPV4;
    a->ip_address = in->sin_addr.s_addr;
    a->port = in->sin_port;
    inet_ntop(AF_INET, &in->sin_addr, a->string, sizeof(a->string));
}

IMPORT(s3eSocketCreate) {
    uint32_t type = arg(0);
    int kind = type == TYPE_TCP ? SOCK_STREAM : type == TYPE_UDP ? SOCK_DGRAM : -1;
    if (kind < 0 || (arg(1) != 0 && arg(1) != 2)) {
        g_error = ERR_UNSUPPORTED;
        ret(0);
        return;
    }
    native_socket fd = socket(AF_INET, kind, 0);
#ifdef _WIN32
    if (fd == INVALID_SOCKET) {
#else
    if (fd < 0) {
#endif
        fail();
        ret(0);
        return;
    }
    set_nonblocking(fd);
    if (type == TYPE_UDP) {
        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof(on));
    }
    g_error = ERR_NONE;
    ret(new_slot(fd, (int32_t)type));
}

IMPORT(s3eSocketClose) {
    struct slot *s = slot_of(arg(0));
    if (!s) {
        ret(RESULT_ERROR);
        return;
    }
    close_native(s->fd);
    uint32_t generation = s->generation;
    memset(s, 0, sizeof(*s));
    s->generation = generation;
    ret(RESULT_SUCCESS);
}

IMPORT(s3eSocketBind) {
    struct slot *s = slot_of(arg(0));
    struct sockaddr_in addr;
    if (!s || to_sockaddr(arg(1), &addr) < 0) {
        ret(RESULT_ERROR);
        return;
    }
    if (arg(2) & 0xff) {
        int on = 1;
        setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
    }
    if (bind(s->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ret(fail());
        return;
    }
    g_error = ERR_NONE;
    ret(RESULT_SUCCESS);
}

IMPORT(s3eSocketListen) {
    struct slot *s = slot_of(arg(0));
    if (!s) {
        ret(RESULT_ERROR);
        return;
    }
    ret(listen(s->fd, (int)(arg(1) & 0xffff)) < 0 ? fail() : RESULT_SUCCESS);
}

IMPORT(s3eSocketAccept) {
    struct slot *s = slot_of(arg(0));
    if (!s) {
        ret(0);
        return;
    }
    struct sockaddr_in peer;
    socklen_t len = sizeof(peer);
    native_socket fd = accept(s->fd, (struct sockaddr *)&peer, &len);
#ifdef _WIN32
    if (fd == INVALID_SOCKET) {
#else
    if (fd < 0) {
#endif
        int32_t e = map_errno(last_error());
        if (e == ERR_WOULDBLOCK && arg(2)) {
            s->accept_cb = arg(2);
            s->accept_ud = arg(3);
            s->readable_cb = 0;
        }
        g_error = e;
        ret(0);
        return;
    }
    set_nonblocking(fd);
    s->accept_cb = 0;
    from_sockaddr(&peer, arg(1));
    g_error = ERR_NONE;
    ret(new_slot(fd, TYPE_TCP));
}

IMPORT(s3eSocketConnect) {
    struct slot *s = slot_of(arg(0));
    struct sockaddr_in addr;
    if (!s || to_sockaddr(arg(1), &addr) < 0) {
        ret(RESULT_ERROR);
        return;
    }
    if (connect(s->fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        s->connect_pending = arg(2) != 0;
        s->connect_cb = arg(2);
        s->connect_ud = arg(3);
        g_error = ERR_NONE;
        ret(RESULT_SUCCESS);
        return;
    }
    int32_t e = map_errno(last_error());
    if (e == ERR_INPROGRESS || e == ERR_ALREADY || e == ERR_WOULDBLOCK) {
        s->connect_pending = 1;
        s->connect_cb = arg(2);
        s->connect_ud = arg(3);
        g_error = ERR_INPROGRESS;
        ret(RESULT_SUCCESS);
        return;
    }
    g_error = e;
    ret(RESULT_ERROR);
}

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

IMPORT(s3eSocketSend) {
    struct slot *s = slot_of(arg(0));
    if (!s) {
        ret((uint32_t)-1);
        return;
    }
    long sent = (long)send(s->fd, G2H(arg(1)), arg(2), MSG_NOSIGNAL);
    if (sent < 0) {
        fail();
    } else {
        g_error = ERR_NONE;
    }
    ret((uint32_t)sent);
}

IMPORT(s3eSocketSendTo) {
    struct slot *s = slot_of(arg(0));
    struct sockaddr_in addr;
    if (!s || to_sockaddr(arg(4), &addr) < 0) {
        ret((uint32_t)-1);
        return;
    }
    long sent = (long)sendto(s->fd, G2H(arg(1)), arg(2), MSG_NOSIGNAL, (struct sockaddr *)&addr,
                             sizeof(addr));
    if (sent < 0) {
        fail();
    } else {
        g_error = ERR_NONE;
    }
    ret((uint32_t)sent);
}

IMPORT(s3eSocketRecv) {
    struct slot *s = slot_of(arg(0));
    if (!s) {
        ret((uint32_t)-1);
        return;
    }
    long got = (long)recv(s->fd, G2H(arg(1)), arg(2), 0);
    if (got < 0) {
        fail();
    } else {
        g_error = ERR_NONE;
    }
    ret((uint32_t)got);
}

IMPORT(s3eSocketRecvFrom) {
    struct slot *s = slot_of(arg(0));
    if (!s) {
        ret((uint32_t)-1);
        return;
    }
    struct sockaddr_in from;
    socklen_t len = sizeof(from);
    long got = (long)recvfrom(s->fd, G2H(arg(1)), arg(2), 0, (struct sockaddr *)&from, &len);
    if (got < 0) {
        fail();
    } else {
        from_sockaddr(&from, arg(4));
        g_error = ERR_NONE;
    }
    ret((uint32_t)got);
}

IMPORT(s3eSocketReadable) {
    struct slot *s = slot_of(arg(0));
    if (!s) {
        ret(RESULT_ERROR);
        return;
    }
    s->readable_cb = arg(1);
    s->readable_ud = arg(1) ? arg(2) : 0;
    s->accept_cb = 0;
    ret(RESULT_SUCCESS);
}

IMPORT(s3eSocketWritable) {
    struct slot *s = slot_of(arg(0));
    if (!s) {
        ret(RESULT_ERROR);
        return;
    }
    s->writable_cb = arg(1);
    s->writable_ud = arg(1) ? arg(2) : 0;
    ret(RESULT_SUCCESS);
}

static uint32_t socket_name(int peer) {
    struct slot *s = slot_of(arg(0));
    if (!s || !arg(1)) {
        g_error = ERR_PARAM;
        return RESULT_ERROR;
    }
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int rc = peer ? getpeername(s->fd, (struct sockaddr *)&addr, &len)
                  : getsockname(s->fd, (struct sockaddr *)&addr, &len);
    if (rc < 0) {
        return fail();
    }
    from_sockaddr(&addr, arg(1));
    return RESULT_SUCCESS;
}

IMPORT(s3eSocketGetLocalName) {
    ret(socket_name(0));
}

IMPORT(s3eSocketGetPeerName) {
    ret(socket_name(1));
}

IMPORT(s3eSocketGetInt) {
    switch (arg(0)) {
    case 0: /* MAX_SOCKETS */
        ret(SLOT_COUNT);
        break;
    case 1: /* NETWORK_AVAILABLE */
    case 6: /* UDP_AVAILABLE */
        ret(1);
        break;
    case 2: /* NETWORK_TYPE: WLAN */
        ret(3);
        break;
    default:
        g_error = ERR_PARAM;
        ret((uint32_t)-1);
        break;
    }
}

IMPORT(s3eSocketGetError) {
    ret((uint32_t)g_error);
}

IMPORT(s3eSocketGetString) {
    static uint32_t hostname, empty;
    if (!empty) {
        char name[256] = "";
        gethostname(name, sizeof(name) - 1);
        hostname = arena_strdup(name);
        empty = arena_strdup("");
    }
    ret(arg(0) == 4 ? hostname : empty);
}

static uint32_t bswap32(uint32_t v) {
    return (v >> 24) | ((v >> 8) & 0xff00u) | ((v << 8) & 0xff0000u) | (v << 24);
}

IMPORT(s3eInetHtonl) {
    ret(bswap32(arg(0)));
}

IMPORT(s3eInetHtons) {
    uint32_t v = arg(0) & 0xffffu;
    ret(((v >> 8) | (v << 8)) & 0xffffu);
}

IMPORT(s3eInetAton) {
    struct in_addr parsed;
    const char *text = G2H(arg(1));
    if (!arg(0) || !text || inet_pton(AF_INET, text, &parsed) != 1) {
        g_error = ERR_PARAM;
        ret(RESULT_ERROR);
        return;
    }
    *(uint32_t *)G2H(arg(0)) = parsed.s_addr;
    ret(RESULT_SUCCESS);
}

IMPORT(s3eInetNtoa) {
    struct in_addr a = {.s_addr = arg(0)};
    uint32_t buffer = arg(1);
    int32_t len = (int32_t)arg(2);
    if (!buffer) {
        len = 16;
        buffer = gmalloc(16);
    }
    if (len <= 0 || !inet_ntop(AF_INET, &a, G2H(buffer), (socklen_t)len)) {
        ret(0);
        return;
    }
    ret(buffer);
}

IMPORT(s3eInetToString) {
    static uint32_t buffer;
    if (!buffer) {
        buffer = H2G(arena_alloc(64));
    }
    const struct inet_address *a = G2H(arg(0));
    if (!a) {
        ret(0);
        return;
    }
    char ip[32];
    inet_ntop(AF_INET, &a->ip_address, ip, sizeof(ip));
    if (arg(1)) {
        snprintf(G2H(buffer), 64, "%s:%u", ip, (unsigned)((a->port >> 8) | ((a->port & 0xff) << 8)));
    } else {
        snprintf(G2H(buffer), 64, "%s", ip);
    }
    ret(buffer);
}

IMPORT(s3eInetLookup) {
    /* Only numeric addresses resolve: the game's servers no longer exist. */
    const char *host = G2H(arg(0));
    struct inet_address *a = G2H(arg(1));
    struct in_addr parsed;
    if (host && a && inet_pton(AF_INET, host, &parsed) == 1) {
        uint16_t port = a->port;
        memset(a, 0, sizeof(*a));
        a->type = ADDR_IPV4;
        a->ip_address = parsed.s_addr;
        a->port = port;
        snprintf(a->string, sizeof(a->string), "%s", host);
        if (arg(2)) {
            /* deliver on the next pump with the address as system data */
            g_lookup_cb = arg(2);
            g_lookup_ud = arg(3);
            g_lookup_addr = arg(1);
        }
        ret(RESULT_SUCCESS);
        return;
    }
    log_debug("lookup of %s skipped (offline)", host ? host : "?");
    g_error = ERR_UNKNOWN_HOST;
    if (arg(2)) {
        g_lookup_cb = arg(2);
        g_lookup_ud = arg(3);
        g_lookup_addr = 0;
        ret(RESULT_SUCCESS);
        return;
    }
    ret(RESULT_ERROR);
}

IMPORT(s3eInetLookupCancel) {
    g_lookup_cb = 0;
}

static void dispatch(uint32_t cb, uint32_t handle, uint32_t system_data, uint32_t ud) {
    if (cb) {
        guest_call3(cb, handle, system_data, ud);
    }
}

void zeroconf_pump(void);

void socket_pump(void) {
    zeroconf_pump();
    if (g_lookup_cb) {
        uint32_t cb = g_lookup_cb, ud = g_lookup_ud, addr = g_lookup_addr;
        g_lookup_cb = 0;
        g_error = addr ? ERR_NONE : ERR_UNKNOWN_HOST;
        guest_call2(cb, addr, ud);
    }
#ifndef _WIN32
    struct pollfd fds[SLOT_COUNT];
    uint32_t index[SLOT_COUNT], generation[SLOT_COUNT];
    nfds_t count = 0;
    for (uint32_t i = 0; i < SLOT_COUNT; ++i) {
        struct slot *s = &g_slots[i];
        short events = 0;
        if (!s->in_use) {
            continue;
        }
        if (s->accept_cb || s->readable_cb) {
            events |= POLLIN;
        }
        if (s->connect_pending || s->writable_cb) {
            events |= POLLOUT;
        }
        if (events) {
            fds[count] = (struct pollfd){s->fd, events, 0};
            index[count] = i;
            generation[count] = s->generation;
            count++;
        }
    }
    if (!count || poll(fds, count, 0) <= 0) {
        return;
    }
    for (nfds_t k = 0; k < count; ++k) {
        short ev = fds[k].revents;
        uint32_t i = index[k];
        struct slot *s = &g_slots[i];
        uint32_t handle = HANDLE_FIRST + i;
        if (!ev || !s->in_use || s->generation != generation[k]) {
            continue;
        }
        if (s->connect_pending && (ev & (POLLOUT | POLLERR | POLLHUP))) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &err, &len);
            uint32_t cb = s->connect_cb, ud = s->connect_ud;
            s->connect_pending = 0;
            s->connect_cb = 0;
            uint32_t result = gmalloc(4);
            *(int32_t *)G2H(result) = err ? RESULT_ERROR : RESULT_SUCCESS;
            g_error = map_errno(err);
            dispatch(cb, handle, result, ud);
            gfree(result);
        }
        if (s->in_use && s->accept_cb && (ev & (POLLIN | POLLERR | POLLHUP))) {
            uint32_t cb = s->accept_cb, ud = s->accept_ud;
            s->accept_cb = 0;
            dispatch(cb, handle, 0, ud);
        }
        if (s->in_use && s->readable_cb && (ev & (POLLIN | POLLERR | POLLHUP))) {
            uint32_t cb = s->readable_cb, ud = s->readable_ud;
            s->readable_cb = 0;
            dispatch(cb, handle, 0, ud);
        }
        if (s->in_use && s->writable_cb && (ev & (POLLOUT | POLLERR | POLLHUP))) {
            uint32_t cb = s->writable_cb, ud = s->writable_ud;
            s->writable_cb = 0;
            dispatch(cb, handle, 0, ud);
        }
    }
#endif
}

void register_socket_imports(void) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    REG(s3eSocketCreate);
    REG(s3eSocketClose);
    REG(s3eSocketBind);
    REG(s3eSocketListen);
    REG(s3eSocketAccept);
    REG(s3eSocketConnect);
    REG(s3eSocketSend);
    REG(s3eSocketSendTo);
    REG(s3eSocketRecv);
    REG(s3eSocketRecvFrom);
    REG(s3eSocketReadable);
    REG(s3eSocketWritable);
    REG(s3eSocketGetLocalName);
    REG(s3eSocketGetPeerName);
    REG(s3eSocketGetInt);
    REG(s3eSocketGetError);
    REG(s3eSocketGetString);
    REG(s3eInetHtonl);
    REG(s3eInetHtons);
    REG(s3eInetAton);
    REG(s3eInetNtoa);
    REG(s3eInetToString);
    REG(s3eInetLookup);
    REG(s3eInetLookupCancel);
    import_register("s3eInetNtohl", imp_s3eInetHtonl);
    import_register("s3eInetNtohs", imp_s3eInetHtons);
}
