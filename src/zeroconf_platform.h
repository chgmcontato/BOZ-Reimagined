#ifndef CODBOZ_ZEROCONF_PLATFORM_H
#define CODBOZ_ZEROCONF_PLATFORM_H

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

uint64_t zeroconf_platform_now_ms(void);
/* Selection and send return boolean success; open returns a descriptor or -1. */
int zeroconf_platform_select_ipv4(struct in_addr *selected);
int zeroconf_platform_open_socket(struct in_addr *selected);
int zeroconf_platform_send(int socket_fd, const uint8_t *packet, size_t packet_size,
                           const struct sockaddr_in *destination);
/* Receive returns a byte count or -1 with errno set. */
ssize_t zeroconf_platform_receive(int socket_fd, uint8_t *packet, size_t capacity,
                                  struct sockaddr_in *source);
void zeroconf_platform_close_socket(int socket_fd);
/* Every up, multicast-capable, non-loopback IPv4 interface (address and netmask); returns the count.
 * mDNS is sent on each of them: the TV may sit on another LAN (e.g. a ZeroTier network). */
int zeroconf_platform_list_ipv4(struct in_addr *addresses, struct in_addr *netmasks, int capacity);
/* Selects the interface for the next multicast send; boolean success. */
int zeroconf_platform_set_multicast_interface(int socket_fd, struct in_addr interface_address);

#endif
