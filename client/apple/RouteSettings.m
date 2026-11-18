#import "RouteSettings.h"
#import "ProtocolValidation.h"
#include <arpa/inet.h>

static BOOL Routes(NSArray<NSString *> *values, NSMutableArray *v4, NSMutableArray *v6) {
    if (values.count > 4096) return NO;
    for (NSString *value in values) {
        NSArray<NSString *> *parts = [value componentsSeparatedByString:@"/"];
        if (parts.count != 2) return NO;
        struct in_addr address, mask;
        struct in6_addr address6;
        if (inet_pton(AF_INET, parts[0].UTF8String, &address) == 1) {
            uint32_t bits;
            if (inet_pton(AF_INET, parts[1].UTF8String, &mask) == 1) {
                bits = ntohl(mask.s_addr);
                uint64_t inverse = (uint32_t)~bits;
                if (inverse & (inverse + 1)) return NO;
            } else {
                int prefix = vpn_prefix6(parts[1].UTF8String);
                if (prefix < 0 || prefix > 32) return NO;
                bits = prefix ? UINT32_MAX << (32 - prefix) : 0;
                mask.s_addr = htonl(bits);
            }
            address.s_addr &= mask.s_addr;
            char network[INET_ADDRSTRLEN], netmask[INET_ADDRSTRLEN];
            if (!inet_ntop(AF_INET, &address, network, sizeof(network)) || !inet_ntop(AF_INET, &mask, netmask, sizeof(netmask))) return NO;
            [v4 addObject:[[NEIPv4Route alloc] initWithDestinationAddress:@(network) subnetMask:@(netmask)]];
        } else if (inet_pton(AF_INET6, parts[0].UTF8String, &address6) == 1) {
            int prefix = vpn_prefix6(parts[1].UTF8String);
            if (prefix < 0) return NO;
            for (int bit = prefix; bit < 128; ++bit) address6.s6_addr[bit / 8] &= (uint8_t)~(1u << (7 - bit % 8));
            char network[INET6_ADDRSTRLEN];
            if (!inet_ntop(AF_INET6, &address6, network, sizeof(network))) return NO;
            [v6 addObject:[[NEIPv6Route alloc] initWithDestinationAddress:@(network) networkPrefixLength:@(prefix)]];
        } else return NO;
    }
    return YES;
}

static BOOL FullIPv4(NSArray<NEIPv4Route *> *routes) {
    NSMutableArray<NSArray<NSNumber *> *> *ranges = [NSMutableArray array];
    for (NEIPv4Route *route in routes) {
        struct in_addr address, mask;
        if (inet_pton(AF_INET, route.destinationAddress.UTF8String, &address) != 1 ||
            inet_pton(AF_INET, route.destinationSubnetMask.UTF8String, &mask) != 1) return NO;
        uint64_t first = ntohl(address.s_addr);
        [ranges addObject:@[@(first), @(first + (uint32_t)~ntohl(mask.s_addr) + 1)]];
    }
    [ranges sortUsingComparator:^NSComparisonResult(NSArray *a, NSArray *b) { return [a[0] compare:b[0]]; }];
    uint64_t end = 0;
    for (NSArray<NSNumber *> *range in ranges) {
        if (range[0].unsignedLongLongValue > end) return NO;
        end = MAX(end, range[1].unsignedLongLongValue);
    }
    return end == (UINT64_C(1) << 32);
}

BOOL VPNApplyRoutes(NEPacketTunnelNetworkSettings *settings, NSArray<NSString *> *includes,
                    NSArray<NSString *> *excludes, BOOL assignedIPv6, NSError **error) {
    NSMutableArray *in4 = [NSMutableArray array], *in6 = [NSMutableArray array];
    NSMutableArray *out4 = [NSMutableArray array], *out6 = [NSMutableArray array];
    if (!Routes(includes, in4, in6) || !Routes(excludes, out4, out6)) {
        if (error) *error = [NSError errorWithDomain:@"io.github.sck03.lansyra" code:8
            userInfo:@{NSLocalizedDescriptionKey:@"服务器路由无效或超过数量限制"}];
        return NO;
    }
    if (!in4.count) [in4 addObject:NEIPv4Route.defaultRoute];
    if (assignedIPv6) {
        if (!in6.count) [in6 addObject:NEIPv6Route.defaultRoute];
    } else {
        [in6 removeAllObjects]; [out6 removeAllObjects];
        // Protect an IPv4-only full tunnel without blocking unrelated IPv6 for split VPNs.
        if (FullIPv4(in4)) [in6 addObject:NEIPv6Route.defaultRoute];
    }
    for (NSString *dns in settings.DNSSettings.servers) {
        NSMutableArray *dns4 = [NSMutableArray array], *dns6 = [NSMutableArray array];
        NSString *host = [dns stringByAppendingString:[dns containsString:@":"] ? @"/128" : @"/32"];
        if (!Routes(@[host], dns4, dns6) || (!assignedIPv6 && dns6.count)) {
            if (error) *error = [NSError errorWithDomain:@"io.github.sck03.lansyra" code:8
                userInfo:@{NSLocalizedDescriptionKey:@"服务器 DNS 地址不可达或无效"}];
            return NO;
        }
        [in4 addObjectsFromArray:dns4]; [in6 addObjectsFromArray:dns6];
        [out4 filterUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(NEIPv4Route *route, NSDictionary *bindings) {
            (void)bindings; return !([route.destinationAddress isEqualToString:dns] && [route.destinationSubnetMask isEqualToString:@"255.255.255.255"]);
        }]];
        [out6 filterUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(NEIPv6Route *route, NSDictionary *bindings) {
            (void)bindings; return !([route.destinationAddress isEqualToString:dns] && route.destinationNetworkPrefixLength.intValue == 128);
        }]];
    }
    settings.IPv4Settings.includedRoutes = in4;
    settings.IPv4Settings.excludedRoutes = out4;
    settings.IPv6Settings.includedRoutes = in6;
    settings.IPv6Settings.excludedRoutes = out6;
    return YES;
}
