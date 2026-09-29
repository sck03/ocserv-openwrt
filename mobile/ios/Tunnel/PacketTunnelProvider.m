#import <NetworkExtension/NetworkExtension.h>
#include <openconnect.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <sys/uio.h>
#include "ProtocolValidation.h"

@interface PacketTunnelProvider : NEPacketTunnelProvider
@property(nonatomic, assign) struct openconnect_info *vpn;
@property(nonatomic) int commandFD;
@property(nonatomic) int packetFD;
@property(atomic) BOOL stopping;
@property(nonatomic) BOOL running; // protected by @synchronized(self)
@property(nonatomic, copy) NSString *username;
@property(nonatomic, copy) NSString *password;
@property(nonatomic, copy) NSString *pin;
@property(nonatomic) NSUInteger authAttempts;
@property(nonatomic, strong) dispatch_queue_t packets;
@property(nonatomic, strong) dispatch_source_t reader;
@property(nonatomic, copy) void (^stopCompletion)(void);
@end

static NSError *VPNError(NSString *message) {
    return [NSError errorWithDomain:@"com.bulijie.vpn" code:1
                          userInfo:@{NSLocalizedDescriptionKey: message}];
}

static int ValidateCertificate(void *data, const char *reason) {
    PacketTunnelProvider *provider = (__bridge PacketTunnelProvider *)data;
    // No trust-on-first-use: the administrator's full SHA-256 pin is mandatory.
    return openconnect_check_peer_cert_hash(provider.vpn, provider.pin.UTF8String);
}

static int Authenticate(void *data, struct oc_auth_form *form) {
    PacketTunnelProvider *provider = (__bridge PacketTunnelProvider *)data;
    if (provider.stopping || (form->error && form->error[0]) || ++provider.authAttempts > 3 ||
        openconnect_check_peer_cert_hash(provider.vpn, provider.pin.UTF8String))
        return OC_FORM_RESULT_CANCELLED;
    for (struct oc_form_opt *opt = form->opts; opt; opt = opt->next) {
        if ((opt->flags & OC_FORM_OPT_IGNORE) || opt->type == OC_FORM_OPT_HIDDEN) continue;
        const char *value = NULL;
        if (!opt->name) return OC_FORM_RESULT_CANCELLED;
        if (opt->type == OC_FORM_OPT_TEXT && !strcmp(opt->name, "username"))
            value = provider.username.UTF8String;
        else if (opt->type == OC_FORM_OPT_PASSWORD && !strcmp(opt->name, "password"))
            value = provider.password.UTF8String;
        else
            return OC_FORM_RESULT_CANCELLED; // Never put a password into an unknown/MFA field.
        if (openconnect_set_option_value(opt, value)) return OC_FORM_RESULT_ERR;
    }
    return OC_FORM_RESULT_OK;
}

static void Progress(void *data, int level, const char *format, ...) {
    // Core messages may contain authentication material. Do not persist or print them.
}

@implementation PacketTunnelProvider

- (void)startTunnelWithOptions:(NSDictionary<NSString *,NSObject *> *)options
            completionHandler:(void (^)(NSError *))completionHandler {
    @synchronized (self) {
        if (self.running) {
            completionHandler(VPNError(@"上一个连接尚未结束"));
            return;
        }
        self.running = YES;
        self.stopping = NO;
        self.commandFD = -1;
    }
    self.packetFD = -1;
    self.authAttempts = 0;
    NETunnelProviderProtocol *config = (NETunnelProviderProtocol *)self.protocolConfiguration;
    NSDictionary *values = config.providerConfiguration;
    self.username = [values[@"username"] isKindOfClass:NSString.class] ? values[@"username"] : @"";
    self.pin = [values[@"pin"] isKindOfClass:NSString.class] ? values[@"pin"] : @"";
    self.password = [options[@"password"] isKindOfClass:NSString.class] ? (NSString *)options[@"password"] : @"";
    NSURL *server = [NSURL URLWithString:config.serverAddress ?: @""];
    NSData *hash = [self.pin hasPrefix:@"pin-sha256:"] ?
        [[NSData alloc] initWithBase64EncodedString:[self.pin substringFromIndex:11] options:0] : nil;
    if (![server.scheme isEqualToString:@"https"] || !server.host.length ||
        server.user || server.password || server.fragment || hash.length != 32 ||
        ![[hash base64EncodedStringWithOptions:0] isEqualToString:[self.pin substringFromIndex:11]] ||
        !self.username.length || !self.password.length) {
        self.password = nil;
        void (^stopped)(void);
        @synchronized (self) {
            self.running = NO;
            stopped = self.stopCompletion;
            self.stopCompletion = nil;
        }
        completionHandler(VPNError(@"服务器、账号、密码或指纹无效；自动连接需重新输入密码。"));
        if (stopped) stopped();
        return;
    }
    self.packets = dispatch_queue_create("com.bulijie.vpn.packets", DISPATCH_QUEUE_SERIAL);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
            [self runServer:config.serverAddress completion:completionHandler];
        }
    });
}

- (void)runServer:(NSString *)server completion:(void (^)(NSError *))completion {
    NSError *failure = nil;
    BOOL started = NO;
    int coreTunFD = -1;
    int sslResult = openconnect_init_ssl();
    @synchronized (self) {
        if (!self.stopping && !sslResult) self.vpn = openconnect_vpninfo_new("BulijieVPN-iOS", ValidateCertificate, NULL,
                                         Authenticate, Progress, (__bridge void *)self);
        if (self.vpn) self.commandFD = openconnect_setup_cmd_pipe(self.vpn);
    }
    do {
        if (self.stopping || !self.vpn || self.commandFD < 0) {
            failure = VPNError(@"无法启动 VPN 核心"); break;
        }
        // The packaged OpenSSL has no iOS root store. Trust only the explicitly supplied pin.
        openconnect_set_system_trust(self.vpn, 0);
        if (openconnect_set_protocol(self.vpn, "anyconnect") ||
            openconnect_parse_url(self.vpn, server.UTF8String) ||
            openconnect_obtain_cookie(self.vpn) || self.stopping) {
            failure = VPNError(@"认证失败：请检查证书指纹、用户名和密码；此版本不支持多因素交互认证。"); break;
        }
        self.password = nil;
        if (openconnect_make_cstp_connection(self.vpn)) {
            failure = VPNError(@"无法建立 VPN 隧道"); break;
        }
        const struct oc_ip_info *info = NULL;
        if (openconnect_get_ip_info(self.vpn, &info, NULL, NULL) || !info || !info->addr || !info->netmask) {
            failure = VPNError(@"服务器未分配 IPv4 地址"); break;
        }
        NSString *gateway = info->gateway_addr ? @(info->gateway_addr) : nil;
        if (!gateway.length) { failure = VPNError(@"服务器地址不可用"); break; }
        NEPacketTunnelNetworkSettings *settings = [[NEPacketTunnelNetworkSettings alloc] initWithTunnelRemoteAddress:gateway];
        if (info->mtu < 1280 || info->mtu > 65535) {
            failure = VPNError(@"服务器 MTU 无效；全隧道 IPv6 需要至少 1280 字节"); break;
        }
        settings.MTU = @(MIN(info->mtu, 1400));
        settings.IPv4Settings = [[NEIPv4Settings alloc] initWithAddresses:@[@(info->addr)] subnetMasks:@[@(info->netmask)]];
        settings.IPv4Settings.includedRoutes = @[[NEIPv4Route defaultRoute]];
        // Capture IPv6 as well. When ocserv supplies none, IPv6 has no usable upstream
        // and is dropped inside the tunnel instead of escaping over the physical link.
        NSArray<NSString *> *parts = info->netmask6 ? [@(info->netmask6) componentsSeparatedByString:@"/"] : @[];
        // Modern ocserv may send only X-CSTP-Address-IP6 (address/prefix).
        NSString *v6 = info->addr6 ? @(info->addr6) : (parts.count == 2 ? parts[0] : @"fd00::2");
        if ([v6 containsString:@"/"]) v6 = [v6 componentsSeparatedByString:@"/"][0];
        int prefixLength = parts.count == 2 ? bvpn_prefix6(parts[1].UTF8String) : 128;
        struct in6_addr parsedV6;
        if (parts.count > 2 || prefixLength < 0 || inet_pton(AF_INET6, v6.UTF8String, &parsedV6) != 1) {
            failure = VPNError(@"服务器 IPv6 前缀无效"); break;
        }
        NSNumber *prefix = @(prefixLength);
        settings.IPv6Settings = [[NEIPv6Settings alloc] initWithAddresses:@[v6] networkPrefixLengths:@[prefix]];
        settings.IPv6Settings.includedRoutes = @[[NEIPv6Route defaultRoute]];
        NSMutableArray *dns = [NSMutableArray array];
        for (int i = 0; i < 3; i++) if (info->dns[i]) [dns addObject:@(info->dns[i])];
        if (!dns.count) { failure = VPNError(@"服务器未提供 DNS"); break; }
        settings.DNSSettings = [[NEDNSSettings alloc] initWithServers:dns];
        settings.DNSSettings.matchDomains = @[@""];
        dispatch_semaphore_t ready = dispatch_semaphore_create(0);
        __block NSError *settingsError = nil;
        [self setTunnelNetworkSettings:settings completionHandler:^(NSError *error) {
            settingsError = error;
            dispatch_semaphore_signal(ready);
        }];
        long waitResult = 1;
        for (unsigned attempt = 0; attempt < 150 && !self.stopping; ++attempt) {
            waitResult = dispatch_semaphore_wait(ready, dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC));
            if (!waitResult) break;
        }
        if (self.stopping) break;
        if (waitResult) {
            failure = VPNError(@"应用网络设置超时"); break;
        }
        failure = settingsError;
        if (failure || self.stopping) break;
        int pair[2];
        if (socketpair(AF_UNIX, SOCK_DGRAM, 0, pair)) { failure = VPNError(@"无法创建数据通道"); break; }
        // Caller-supplied TUN descriptors without a vpnc script remain caller-owned.
        coreTunFD = pair[0];
        if (openconnect_setup_tun_fd(self.vpn, pair[0])) {
            close(pair[1]); failure = VPNError(@"无法配置数据通道"); break;
        }
        int flags = fcntl(pair[1], F_GETFL, 0);
        if (flags < 0 || fcntl(pair[1], F_SETFL, flags | O_NONBLOCK)) {
            close(pair[1]); failure = VPNError(@"无法配置非阻塞数据通道"); break;
        }
        int packetFD = pair[1];
        dispatch_sync(self.packets, ^{ [self startPackets:packetFD]; });
        // DTLS failure falls back to the already established TLS tunnel.
        openconnect_setup_dtls(self.vpn, 60);
        started = YES;
        completion(nil);
        int result = openconnect_mainloop(self.vpn, 300, 10);
        if (!self.stopping) failure = VPNError(result ? @"VPN 连接中断" : @"VPN 连接已结束");
    } while (NO);
    self.password = nil;
    dispatch_sync(self.packets, ^{
        self.packetFD = -1;
        if (self.reader) { dispatch_source_cancel(self.reader); self.reader = nil; }
    });
    void (^stopped)(void);
    @synchronized (self) {
        self.commandFD = -1;
        if (self.vpn) { openconnect_vpninfo_free(self.vpn); self.vpn = NULL; }
        stopped = self.stopCompletion;
        self.stopCompletion = nil;
        self.running = NO;
    }
    if (coreTunFD >= 0) close(coreTunFD);
    if (!started) completion(failure ?: VPNError(@"连接已取消"));
    else if (failure && !self.stopping) [self cancelTunnelWithError:failure];
    if (stopped) stopped();
}

- (void)startPackets:(int)fd {
    self.packetFD = fd;
    self.reader = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, fd, 0, self.packets);
    __weak PacketTunnelProvider *weakSelf = self;
    dispatch_source_set_event_handler(self.reader, ^{
        PacketTunnelProvider *provider = weakSelf;
        if (!provider || provider.stopping || provider.packetFD != fd) return;
        unsigned char bytes[65540];
        NSMutableArray *packets = [NSMutableArray arrayWithCapacity:32];
        NSMutableArray *protocols = [NSMutableArray arrayWithCapacity:32];
        // Bound each event so disconnect/cleanup cannot starve under sustained traffic.
        for (int i = 0; i < 32 && !provider.stopping; i++) {
            ssize_t count = recv(fd, bytes, sizeof(bytes), 0);
            if (count < 0) break;
            if (count <= 4) continue;
            uint32_t family;
            memcpy(&family, bytes, 4);
            family = ntohl(family);
            if (family != AF_INET && family != AF_INET6) continue;
            if (!bvpn_packet_valid(bytes + 4, (size_t)count - 4, family == AF_INET ? 4 : 6)) continue;
            NSData *packet = [NSData dataWithBytes:bytes + 4 length:(NSUInteger)count - 4];
            [packets addObject:packet];
            [protocols addObject:@(family)];
        }
        if (packets.count) [provider.packetFlow writePackets:packets withProtocols:protocols];
    });
    dispatch_source_set_cancel_handler(self.reader, ^{ close(fd); });
    dispatch_resume(self.reader);
    [self readPackets];
}

- (void)readPackets {
    if (self.stopping || self.packetFD < 0) return;
    __weak PacketTunnelProvider *weakSelf = self;
    [self.packetFlow readPacketsWithCompletionHandler:^(NSArray<NSData *> *packets, NSArray<NSNumber *> *protocols) {
        PacketTunnelProvider *provider = weakSelf;
        if (!provider) return;
        dispatch_async(provider.packets, ^{
            if (provider.stopping || provider.packetFD < 0) return;
            if (packets.count != protocols.count) { [provider readPackets]; return; }
            for (NSUInteger i = 0; i < packets.count; i++) {
                unsigned protocol = protocols[i].unsignedIntValue;
                if (protocol != AF_INET && protocol != AF_INET6) continue;
                if (!bvpn_packet_valid(packets[i].bytes, packets[i].length, protocol == AF_INET ? 4 : 6)) continue;
                uint32_t family = htonl(protocols[i].unsignedIntValue);
                struct iovec vectors[2] = {{&family, sizeof(family)}, {(void *)packets[i].bytes, packets[i].length}};
                struct msghdr message = {0};
                message.msg_iov = vectors; message.msg_iovlen = 2;
                // Darwin OpenConnect expects the four-byte network-order AF prefix.
                // Drop on backpressure, just as a bounded TUN queue does.
                sendmsg(provider.packetFD, &message, 0);
            }
            [provider readPackets];
        });
    }];
}

- (void)stopTunnelWithReason:(NEProviderStopReason)reason completionHandler:(void (^)(void))completionHandler {
    self.stopping = YES;
    BOOL immediate;
    @synchronized (self) {
        immediate = !self.running;
        if (!immediate) {
            self.stopCompletion = completionHandler;
            if (self.commandFD >= 0) { char command = OC_CMD_CANCEL; write(self.commandFD, &command, 1); }
        }
    }
    if (immediate) completionHandler();
}
@end
