/* Windows side of zeroconf_platform.h: Winsock socket, interfaces from GetAdaptersAddresses.
 * Shares code with boz-redux's file of the same name (MIT, Copyright (c) 2026 ZappaVinny and BOZ
 * Redux contributors); see LICENSE-boz-redux. */
#include "zc_compat.h"

#include "zeroconf_platform.h"

#include <iphlpapi.h>

enum {
    MDNS_PORT = 5353,
    MDNS_MULTICAST_ADDRESS = 0xe00000fbu,
    MAX_INTERFACES = 16,
};

uint64_t zeroconf_platform_now_ms(void) {
    return monotonic_ms();
}

int zeroconf_platform_list_ipv4(struct in_addr *addresses, struct in_addr *netmasks, int capacity) {
    if (capacity <= 0) {
        return 0;
    }
    ULONG size = 16 * 1024;
    IP_ADAPTER_ADDRESSES *list = NULL;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
        free(list);
        list = malloc(size);
        if (!list) {
            return 0;
        }
        result = GetAdaptersAddresses(AF_INET,
                                      GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                          GAA_FLAG_SKIP_DNS_SERVER,
                                      NULL, list, &size);
    }
    int count = 0;
    if (result == NO_ERROR) {
        for (IP_ADAPTER_ADDRESSES *adapter = list; adapter && count < capacity;
             adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp ||
                adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
                (adapter->Flags & IP_ADAPTER_NO_MULTICAST)) {
                continue;
            }
            for (IP_ADAPTER_UNICAST_ADDRESS *unicast = adapter->FirstUnicastAddress;
                 unicast && count < capacity; unicast = unicast->Next) {
                const struct sockaddr_in *candidate =
                    (const struct sockaddr_in *)unicast->Address.lpSockaddr;
                if (!candidate || candidate->sin_family != AF_INET ||
                    candidate->sin_addr.s_addr == htonl(INADDR_ANY)) {
                    continue;
                }
                addresses[count] = candidate->sin_addr;
                if (netmasks) {
                    unsigned prefix = unicast->OnLinkPrefixLength;
                    uint32_t mask = prefix ? 0xffffffffu << (32 - (prefix > 32 ? 32 : prefix)) : 0;
                    netmasks[count].s_addr = htonl(mask);
                }
                ++count;
            }
        }
    }
    free(list);
    return count;
}

int zeroconf_platform_select_ipv4(struct in_addr *selected) {
    return selected && zeroconf_platform_list_ipv4(selected, NULL, 1) == 1;
}

int zeroconf_platform_set_multicast_interface(int socket_fd, struct in_addr interface_address) {
    return setsockopt((SOCKET)socket_fd, IPPROTO_IP, IP_MULTICAST_IF,
                      (const char *)&interface_address, sizeof(interface_address)) == 0;
}

int zeroconf_platform_open_socket(struct in_addr *selected) {
    struct in_addr interfaces[MAX_INTERFACES];
    int count = zeroconf_platform_list_ipv4(interfaces, NULL, MAX_INTERFACES);
    if (count <= 0) {
        return -1;
    }
    SOCKET fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == INVALID_SOCKET) {
        return -1;
    }
    BOOL enabled = TRUE;
    DWORD ttl = 255;
    DWORD loop = 1;
    u_long nonblocking = 1;
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(MDNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    /* SO_REUSEADDR on Windows shares the port with Bonjour/other responders. */
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&enabled, sizeof(enabled));
    if (setsockopt(fd, IPPROTO_IP, IP_TTL, (const char *)&ttl, sizeof(ttl)) != 0 ||
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&ttl, sizeof(ttl)) != 0 ||
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&loop, sizeof(loop)) != 0 ||
        !zeroconf_platform_set_multicast_interface((int)fd, interfaces[0]) ||
        ioctlsocket(fd, FIONBIO, &nonblocking) != 0 ||
        bind(fd, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        closesocket(fd);
        return -1;
    }
    for (int i = 0; i < count; ++i) {
        struct ip_mreq multicast = {
            .imr_multiaddr.s_addr = htonl(MDNS_MULTICAST_ADDRESS),
            .imr_interface = interfaces[i],
        };
        (void)setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&multicast,
                         sizeof(multicast));
    }
    if (selected) {
        *selected = interfaces[0];
    }
    return (int)fd;
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
    return sendto((SOCKET)socket_fd, (const char *)packet, (int)packet_size, 0,
                  (const struct sockaddr *)target, sizeof(*target)) == (int)packet_size;
}

ssize_t zeroconf_platform_receive(int socket_fd, uint8_t *packet, size_t capacity,
                                  struct sockaddr_in *source) {
    if (socket_fd < 0 || !packet || !capacity || !source) {
        return -1;
    }
    int source_length = sizeof(*source);
    int received = recvfrom((SOCKET)socket_fd, (char *)packet, (int)capacity, 0,
                            (struct sockaddr *)source, &source_length);
    return received == SOCKET_ERROR ? -1 : received;
}

void zeroconf_platform_close_socket(int socket_fd) {
    if (socket_fd >= 0) {
        closesocket((SOCKET)socket_fd);
    }
}
