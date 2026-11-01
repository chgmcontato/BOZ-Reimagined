#include "zc_compat.h"

#include "zeroconf_platform.h"

#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

enum {
    MDNS_PORT = 5353,
    MDNS_MULTICAST_ADDRESS = 0xe00000fbu,
};

uint64_t zeroconf_platform_now_ms(void) {
    return monotonic_ms();
}

int zeroconf_platform_select_ipv4(struct in_addr *selected) {
    if (!selected) {
        return 0;
    }
    struct ifaddrs *interfaces = NULL;
    if (getifaddrs(&interfaces) != 0) {
        return 0;
    }
    int found = 0;
    for (const struct ifaddrs *item = interfaces; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET ||
            !(item->ifa_flags & IFF_UP) || !(item->ifa_flags & IFF_RUNNING) ||
            !(item->ifa_flags & IFF_MULTICAST) || (item->ifa_flags & IFF_LOOPBACK)) {
            continue;
        }
        const struct sockaddr_in *candidate = (const struct sockaddr_in *)item->ifa_addr;
        if (candidate->sin_addr.s_addr != htonl(INADDR_ANY)) {
            *selected = candidate->sin_addr;
            found = 1;
            break;
        }
    }
    freeifaddrs(interfaces);
    return found;
}

static int configure_socket(int socket_fd, struct in_addr interface_address) {
    int enabled = 1;
    int ttl = 255;
    unsigned char multicast_ttl = 255;
    unsigned char loop = 1;
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(MDNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    struct ip_mreq multicast = {
        .imr_multiaddr.s_addr = htonl(MDNS_MULTICAST_ADDRESS),
        .imr_interface = interface_address,
    };

    (void)setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
#ifdef SO_REUSEPORT
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &enabled, sizeof(enabled));
#endif
    return setsockopt(socket_fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) == 0 &&
           setsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_TTL, &multicast_ttl,
                      sizeof(multicast_ttl)) == 0 &&
           setsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) == 0 &&
           setsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_IF, &interface_address,
                      sizeof(interface_address)) == 0 &&
           ioctl(socket_fd, FIONBIO, &enabled) == 0 && ioctl(socket_fd, FIOCLEX) == 0 &&
           bind(socket_fd, (const struct sockaddr *)&address, sizeof(address)) == 0 &&
           (setsockopt(socket_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &multicast, sizeof(multicast)) ==
                0 ||
            errno == EADDRINUSE);
}

int zeroconf_platform_list_ipv4(struct in_addr *addresses, struct in_addr *netmasks, int capacity) {
    struct ifaddrs *interfaces = NULL;
    if (capacity <= 0 || getifaddrs(&interfaces) != 0) {
        return 0;
    }
    int count = 0;
    for (const struct ifaddrs *item = interfaces; item && count < capacity; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET ||
            !(item->ifa_flags & IFF_UP) || !(item->ifa_flags & IFF_RUNNING) ||
            !(item->ifa_flags & IFF_MULTICAST) || (item->ifa_flags & IFF_LOOPBACK)) {
            continue;
        }
        const struct sockaddr_in *candidate = (const struct sockaddr_in *)item->ifa_addr;
        if (candidate->sin_addr.s_addr == htonl(INADDR_ANY)) {
            continue;
        }
        addresses[count] = candidate->sin_addr;
        if (netmasks) {
            netmasks[count].s_addr =
                item->ifa_netmask ? ((const struct sockaddr_in *)item->ifa_netmask)->sin_addr.s_addr
                                  : htonl(0xffffff00u);
        }
        ++count;
    }
    freeifaddrs(interfaces);
    return count;
}

int zeroconf_platform_set_multicast_interface(int socket_fd, struct in_addr interface_address) {
    return setsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_IF, &interface_address,
                      sizeof(interface_address)) == 0;
}

int zeroconf_platform_open_socket(struct in_addr *selected) {
    struct in_addr interface_address;
    if (!zeroconf_platform_select_ipv4(&interface_address)) {
        return -1;
    }
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        return -1;
    }
    if (!configure_socket(socket_fd, interface_address)) {
        close(socket_fd);
        return -1;
    }
    /* Also listen on the other interfaces (the first one joined in configure_socket). */
    struct in_addr others[16];
    int count = zeroconf_platform_list_ipv4(others, NULL, 16);
    for (int i = 0; i < count; ++i) {
        if (others[i].s_addr == interface_address.s_addr) {
            continue;
        }
        struct ip_mreq multicast = {
            .imr_multiaddr.s_addr = htonl(MDNS_MULTICAST_ADDRESS),
            .imr_interface = others[i],
        };
        (void)setsockopt(socket_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &multicast, sizeof(multicast));
    }
    if (selected) {
        *selected = interface_address;
    }
    return socket_fd;
}

int zeroconf_platform_send(int socket_fd, const uint8_t *packet, size_t packet_size,
                           const struct sockaddr_in *destination) {
    if (socket_fd < 0 || !packet || !packet_size) {
        return 0;
    }
    struct sockaddr_in multicast = {
        .sin_family = AF_INET,
        .sin_port = htons(MDNS_PORT),
        .sin_addr.s_addr = htonl(MDNS_MULTICAST_ADDRESS),
    };
    const struct sockaddr_in *target = destination ? destination : &multicast;
    return sendto(socket_fd, packet, packet_size, MSG_NOSIGNAL, (const struct sockaddr *)target,
                  sizeof(*target)) == (ssize_t)packet_size;
}

ssize_t zeroconf_platform_receive(int socket_fd, uint8_t *packet, size_t capacity,
                                  struct sockaddr_in *source) {
    if (socket_fd < 0 || !packet || !capacity || !source) {
        return -1;
    }
    socklen_t source_length = sizeof(*source);
    return recvfrom(socket_fd, packet, capacity, 0, (struct sockaddr *)source, &source_length);
}

void zeroconf_platform_close_socket(int socket_fd) {
    if (socket_fd >= 0) {
        close(socket_fd);
    }
}
