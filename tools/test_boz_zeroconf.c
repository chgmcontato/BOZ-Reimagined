/* Regression cases reproduced from the Android TV's LAN announcements. */
#include <assert.h>
#include "../src/zeroconf.c"
static uint8_t captured[2048];
static size_t captured_size;
int zeroconf_platform_send(int fd, const uint8_t *packet, size_t size,
                          const struct sockaddr_in *destination) {
    assert(size <= sizeof(captured));
    memcpy(captured, packet, size); captured_size = size; return 1;
}
int zeroconf_platform_list_ipv4(struct in_addr *a, struct in_addr *m, int n) {
    (void)a; (void)m; (void)n; return 0; /* no interfaces: falls back to the publisher address */
}
int zeroconf_platform_set_multicast_interface(int fd, struct in_addr a) {
    (void)fd; (void)a; return 1;
}
int main(void) {
    struct zero_conf_search search = {0};
    strcpy(search.service_type, "_PROJECT_KIWI._tcp");
    strcpy(search.domain, "local");
    const uint16_t ports[] = {10, 49960, 27050, 65535, 1};
    for (size_t i = 0; i < sizeof(ports)/sizeof(ports[0]); ++i)
        assert(effective_service_port(&search, htons(ports[i])) == ports[i]);
    strcpy(search.service_type, "_http._tcp");
    assert(effective_service_port(&search, 10435) == 10435);
    strcpy(search.service_type, "_PROJECT_KIWI._tcp");
    struct sockaddr_in source = {.sin_family = AF_INET};
    inet_pton(AF_INET, "192.168.14.9", &source.sin_addr);
    struct mdns_wire_packet packet = {0};
    packet.record_count = 1;
    struct mdns_wire_record *r = &packet.records[0];
    strcpy(r->owner, "tv.local");r->data_kind = MDNS_WIRE_RDATA_A;
    r->class_code = MDNS_WIRE_CLASS_IN;r->ttl = 3600;
    const uint8_t reversed[] = {9,14,168,192}, correct[] = {192,168,14,9};
    memcpy(r->data.a.address, reversed, 4);
    apply_address_records(&search, &packet, 100, &source);
    assert(memcmp(search.addresses[0].ipv4, correct, 4) == 0);
    memcpy(r->data.a.address, correct, 4);
    apply_address_records(&search, &packet, 200, &source);
    assert(memcmp(search.addresses[0].ipv4, correct, 4) == 0);
    const uint8_t other[] = {192,168,14,50};
    memcpy(r->data.a.address, other, 4);
    apply_address_records(&search, &packet, 300, &source);
    assert(memcmp(search.addresses[0].ipv4, other, 4) == 0);
    strcpy(search.service_type, "_http._tcp");
    memcpy(r->data.a.address, reversed, 4);
    apply_address_records(&search, &packet, 400, &source);
    assert(memcmp(search.addresses[0].ipv4, reversed, 4) == 0);
    struct zero_conf_publisher publisher = {0};
    strcpy(publisher.service_type, "_PROJECT_KIWI._tcp");
    strcpy(publisher.domain, "local");strcpy(publisher.name, "kino_0");
    strcpy(publisher.host, "test-pc");publisher.port = 49960;
    strcpy(publisher.service_fqdn, "_PROJECT_KIWI._tcp.local");
    strcpy(publisher.instance_fqdn, "kino_0._PROJECT_KIWI._tcp.local");
    strcpy(publisher.host_fqdn, "test-pc.local");
    strcpy(publisher.txt[0], "kino_0");publisher.txt_count = 1;
    memcpy(publisher.ipv4, correct, 4);g_mdns_fd = 1;
    assert(send_publication(&publisher, 0, NULL));
    assert(mdns_wire_parse_packet(captured, captured_size, &packet) == MDNS_WIRE_OK);
    int a = 0, srv = 0, txt = 0;
    assert(packet.flags == 0x8400 && packet.question_count == 0);
    for (size_t i = 0; i < packet.record_count; ++i) {
        r = &packet.records[i];
        if (r->data_kind == MDNS_WIRE_RDATA_SRV) {
            assert(r->data.srv.port == 10435);srv++;
        }
        if (r->data_kind == MDNS_WIRE_RDATA_A) a++;
        if (r->data_kind == MDNS_WIRE_RDATA_TXT) txt++;
    }
    /* native layout: PTR+SRV+TXT answers, no A record, uncompressed (no 0xc0 pointers) */
    assert(packet.record_count == 3 && !a && txt);
    a = 1;
    assert(a && srv);assert(publisher.port == 49960);
    assert(memcmp(publisher.ipv4, correct, 4) == 0);
    puts("PASS: dynamic/fixed ports, alternating A records, unrelated addresses/services, native-compatible publication (swapped SRV port, native PTR/SRV/TXT answer layout)");
    return 0;
}
