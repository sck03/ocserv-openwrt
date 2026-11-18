#import <NetworkExtension/NetworkExtension.h>

/** Apply numeric OpenConnect routes; reject malformed or oversized lists. */
BOOL VPNApplyRoutes(NEPacketTunnelNetworkSettings *settings, NSArray<NSString *> *includes,
                    NSArray<NSString *> *excludes, BOOL assignedIPv6, NSError **error);
