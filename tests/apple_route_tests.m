#import "../client/apple/RouteSettings.h"
#include <assert.h>
#include <stdio.h>

static NEPacketTunnelNetworkSettings *Settings(void) {
    NEPacketTunnelNetworkSettings *settings = [[NEPacketTunnelNetworkSettings alloc] initWithTunnelRemoteAddress:@"192.0.2.1"];
    settings.IPv4Settings = [[NEIPv4Settings alloc] initWithAddresses:@[@"10.77.0.2"] subnetMasks:@[@"255.255.255.0"]];
    settings.IPv6Settings = [[NEIPv6Settings alloc] initWithAddresses:@[@"fd77::2"] networkPrefixLengths:@[@64]];
    settings.DNSSettings = [[NEDNSSettings alloc] initWithServers:@[@"10.77.0.1"]];
    return settings;
}
int main(void) {
    @autoreleasepool {
        NSError *error = nil;
        NEPacketTunnelNetworkSettings *settings = Settings();
        assert(VPNApplyRoutes(settings, @[@"192.168.19.0/24"], @[@"192.168.19.128/25"], NO, &error));
        assert(settings.IPv4Settings.includedRoutes.count == 2);
        assert([settings.IPv4Settings.includedRoutes[0].destinationAddress isEqualToString:@"192.168.19.0"]);
        assert(settings.IPv4Settings.excludedRoutes.count == 1 && settings.IPv6Settings.includedRoutes.count == 0);
        assert(VPNApplyRoutes(settings, @[@"0.0.0.0/1", @"128.0.0.0/1"], @[], NO, &error));
        assert(settings.IPv6Settings.includedRoutes.count == 1);
        assert(VPNApplyRoutes(settings, @[@"fd77::/64"], @[@"fd77::80/128"], YES, &error));
        assert(settings.IPv6Settings.includedRoutes.count == 1 && settings.IPv6Settings.excludedRoutes.count == 1);
        for (NSString *route in @[@"example.com/24", @"10.0.0.0/33", @"10.0.0.0/255.0.255.0", @"fd77::/129"]) {
            assert(!VPNApplyRoutes(settings, @[route], @[], YES, &error) && error);
        }
        printf("Apple split routes, exclusions, DNS reachability and IPv6 protection passed\n");
    }
    return 0;
}
