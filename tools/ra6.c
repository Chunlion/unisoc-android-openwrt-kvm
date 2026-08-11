#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/icmp6.h>
#include <netinet/ip6.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static void stop_running(int signal_number) { (void)signal_number; running = 0; }

#define ADVERTISE_INTERVAL_SECONDS 10
#define ROUTER_LIFETIME_SECONDS 45
#define PREFIX_VALID_LIFETIME_SECONDS 120
#define PREFIX_PREFERRED_LIFETIME_SECONDS 45

struct __attribute__((packed)) advertisement {
    uint8_t type, code;
    uint16_t checksum;
    uint8_t hop_limit, flags;
    uint16_t router_lifetime;
    uint32_t reachable, retransmit;
    uint8_t prefix_type, prefix_length_units, prefix_bits, prefix_flags;
    uint32_t valid_lifetime, preferred_lifetime, reserved;
    struct in6_addr prefix;
    uint8_t source_ll_type, source_ll_length, source_mac[6];
};

struct __attribute__((packed)) frame {
    struct ethhdr ethernet;
    struct ip6_hdr ipv6;
    struct advertisement ra;
};

static int link_local(const char *interface, struct in6_addr *address) {
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) < 0) return -1;
    int result = -1;
    for (struct ifaddrs *item = list; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET6 ||
                strcmp(item->ifa_name, interface) != 0) continue;
        struct sockaddr_in6 *candidate = (struct sockaddr_in6 *)item->ifa_addr;
        if (IN6_IS_ADDR_LINKLOCAL(&candidate->sin6_addr)) {
            *address = candidate->sin6_addr;
            result = 0;
            break;
        }
    }
    freeifaddrs(list);
    return result;
}

static uint16_t checksum(const struct in6_addr *source,
                         const struct in6_addr *destination,
                         const void *payload, size_t length) {
    uint32_t sum = 0;
    const uint8_t *parts[] = {source->s6_addr, destination->s6_addr, payload};
    size_t lengths[] = {16, 16, length};
    for (size_t part = 0; part < 3; part++) {
        for (size_t i = 0; i + 1 < lengths[part]; i += 2)
            sum += ((uint16_t)parts[part][i] << 8) | parts[part][i + 1];
    }
    sum += (uint16_t)(length >> 16) + (uint16_t)length + IPPROTO_ICMPV6;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return htons((uint16_t)~sum);
}

static int send_frame(int fd, const struct sockaddr_ll *destination,
                      struct frame *packet) {
    struct in6_addr source, destination_address;
    memcpy(&source, &packet->ipv6.ip6_src, sizeof(source));
    memcpy(&destination_address, &packet->ipv6.ip6_dst,
           sizeof(destination_address));
    packet->ra.checksum = 0;
    packet->ra.checksum = checksum(&source, &destination_address,
                                   &packet->ra, sizeof(packet->ra));
    return sendto(fd, packet, sizeof(*packet), 0,
                  (const struct sockaddr *)destination, sizeof(*destination));
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s OUTPUT_IF ROUTER_IF PREFIX_ADDRESS\n", argv[0]);
        return 2;
    }
    const char *output_if = argv[1], *router_if = argv[2];
    unsigned output_index = if_nametoindex(output_if);
    if (!output_index) { perror("if_nametoindex"); return 1; }

    int ioctl_fd = socket(AF_INET6, SOCK_DGRAM, 0);
    struct ifreq request = {0};
    snprintf(request.ifr_name, sizeof(request.ifr_name), "%s", router_if);
    if (ioctl_fd < 0 || ioctl(ioctl_fd, SIOCGIFHWADDR, &request) < 0) {
        perror("SIOCGIFHWADDR"); return 1;
    }
    close(ioctl_fd);

    struct frame packet = {0};
    const uint8_t multicast_mac[6] = {0x33, 0x33, 0, 0, 0, 1};
    memcpy(packet.ethernet.h_dest, multicast_mac, 6);
    memcpy(packet.ethernet.h_source, request.ifr_hwaddr.sa_data, 6);
    packet.ethernet.h_proto = htons(ETH_P_IPV6);
    packet.ipv6.ip6_vfc = 0x60;
    packet.ipv6.ip6_plen = htons(sizeof(packet.ra));
    packet.ipv6.ip6_nxt = IPPROTO_ICMPV6;
    packet.ipv6.ip6_hlim = 255;
    struct in6_addr source, multicast;
    if (link_local(router_if, &source) < 0 ||
            inet_pton(AF_INET6, "ff02::1", &multicast) != 1) {
        fprintf(stderr, "missing router link-local address\n"); return 1;
    }
    memcpy(&packet.ipv6.ip6_src, &source, sizeof(source));
    memcpy(&packet.ipv6.ip6_dst, &multicast, sizeof(multicast));
    packet.ra.type = ND_ROUTER_ADVERT;
    packet.ra.hop_limit = 64;
    /* Android's tethering RA is authoritative. This low-preference route is
     * only a fallback for vendor builds that stop advertising after bridging. */
    packet.ra.flags = 0x18; /* RFC 4191 low router preference */
    packet.ra.router_lifetime = htons(ROUTER_LIFETIME_SECONDS);
    packet.ra.prefix_type = ND_OPT_PREFIX_INFORMATION;
    packet.ra.prefix_length_units = 4;
    packet.ra.prefix_bits = 64;
    packet.ra.prefix_flags = 0xc0;
    packet.ra.valid_lifetime = htonl(PREFIX_VALID_LIFETIME_SECONDS);
    packet.ra.preferred_lifetime = htonl(PREFIX_PREFERRED_LIFETIME_SECONDS);
    packet.ra.source_ll_type = ND_OPT_SOURCE_LINKADDR;
    packet.ra.source_ll_length = 1;
    memcpy(packet.ra.source_mac, request.ifr_hwaddr.sa_data, 6);
    if (inet_pton(AF_INET6, argv[3], &packet.ra.prefix) != 1) {
        fprintf(stderr, "invalid prefix: %s\n", argv[3]); return 2;
    }
    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IPV6));
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_ll destination = {
        .sll_family = AF_PACKET,
        .sll_protocol = htons(ETH_P_IPV6),
        .sll_ifindex = (int)output_index,
        .sll_halen = ETH_ALEN,
    };
    memcpy(destination.sll_addr, multicast_mac, 6);
    signal(SIGINT, stop_running);
    signal(SIGTERM, stop_running);
    while (running) {
        if (send_frame(fd, &destination, &packet) < 0)
            perror("sendto");
        for (int elapsed = 0;
             running && elapsed < ADVERTISE_INTERVAL_SECONDS; elapsed++) sleep(1);
    }

    /* Invalidate the fallback router and deprecate the old prefix. Multiple
     * copies make prefix changes reliable on clients in power-save mode. */
    packet.ra.router_lifetime = 0;
    packet.ra.valid_lifetime = 0;
    packet.ra.preferred_lifetime = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (send_frame(fd, &destination, &packet) < 0) perror("withdraw sendto");
        usleep(100000);
    }
    close(fd);
    return 0;
}
