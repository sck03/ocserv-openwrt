#ifndef BVPN_PROTOCOL_VALIDATION_H
#define BVPN_PROTOCOL_VALIDATION_H
#include <stddef.h>
#include <stdint.h>

/* packetFlow and OpenConnect exchange exactly one complete IP datagram. */
static inline int bvpn_packet_valid(const uint8_t *packet, size_t length, unsigned version) {
    if (!packet || !length || packet[0] >> 4 != version) return 0;
    if (version == 4) {
        if (length < 20) return 0;
        size_t header = (packet[0] & 15u) * 4u;
        size_t total = ((size_t)packet[2] << 8) | packet[3];
        return header >= 20 && length >= header && total == length;
    }
    if (version == 6) {
        if (length < 40) return 0;
        size_t payload = ((size_t)packet[4] << 8) | packet[5];
        return length == payload + 40;
    }
    return 0;
}

static inline int bvpn_prefix6(const char *text) {
    if (!text || !*text) return -1;
    unsigned value = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return -1;
        value = value * 10 + (unsigned)(*text - '0');
        if (value > 128) return -1;
    }
    return (int)value;
}
#endif
