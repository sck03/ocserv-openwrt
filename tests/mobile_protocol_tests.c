#include "../mobile/ios/Tunnel/ProtocolValidation.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    uint8_t ipv4[24] = {0x45, 0, 0, 24};
    uint8_t ipv6[48] = {0x60, 0, 0, 0, 0, 8};
    assert(bvpn_packet_valid(ipv4, sizeof(ipv4), 4));
    assert(bvpn_packet_valid(ipv6, sizeof(ipv6), 6));
    assert(!bvpn_packet_valid(NULL, 0, 4));
    assert(!bvpn_packet_valid(ipv4, 19, 4));
    assert(!bvpn_packet_valid(ipv4, sizeof(ipv4), 6));
    assert(!bvpn_packet_valid(ipv6, 47, 6));
    assert(!bvpn_packet_valid(ipv6, 49, 6));
    ipv4[0] = 0x44; /* IHL shorter than the IPv4 header */
    assert(!bvpn_packet_valid(ipv4, sizeof(ipv4), 4));
    ipv4[0] = 0x4f; /* IHL beyond received bytes */
    assert(!bvpn_packet_valid(ipv4, sizeof(ipv4), 4));
    assert(bvpn_prefix6("0") == 0);
    assert(bvpn_prefix6("64") == 64);
    assert(bvpn_prefix6("128") == 128);
    assert(bvpn_prefix6("129") == -1);
    assert(bvpn_prefix6("99999999999999") == -1);
    assert(bvpn_prefix6("64garbage") == -1);
    assert(bvpn_prefix6("-1") == -1);
    assert(bvpn_prefix6("") == -1);
    puts("Mobile packet and prefix validation passed");
    return 0;
}
