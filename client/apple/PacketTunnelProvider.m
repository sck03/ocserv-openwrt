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
#import "ServerAddress.h"
#include <openssl/x509.h>

@interface PacketTunnelProvider : NEPacketTunnelProvider
@property(nonatomic, assign) struct openconnect_info *vpn;
@property(nonatomic) int commandFD;
@property(nonatomic) int packetFD;
@property(atomic) BOOL stopping;
@property(nonatomic) BOOL running; // protected by @synchronized(self)
@property(nonatomic, copy) NSString *username;
@property(nonatomic, copy) NSString *password;
@property(nonatomic, copy) NSString *pin;
@property(nonatomic, strong) NSURL *server;
@property(nonatomic, copy) NSString *sessionID;
@property(nonatomic, copy) NSString *phase;
@property(atomic) NSUInteger generation;
@property(nonatomic) NSUInteger stateGeneration;
@property(atomic) BOOL sleeping;
@property(atomic) BOOL established;
@property(nonatomic) BOOL loopRunning, pauseRequested; // protected by @synchronized(self)
@property(nonatomic, strong) dispatch_semaphore_t resumed;
@property(nonatomic, strong) dispatch_semaphore_t packetClosed;
@property(nonatomic) NSUInteger authAttempts;
@property(nonatomic, strong) dispatch_queue_t packets;
@property(nonatomic, strong) dispatch_source_t reader;
@property(nonatomic, strong) NSMutableArray<dispatch_block_t> *stopCompletions;
@property(nonatomic, strong) NSError *connectionError;
- (NSError *)configureTunnel;
- (void)recordState:(NSString *)phase;
- (void)requestPause;
@end

static NSError *VPNError(NSInteger category, NSString *message) {
    return [NSError errorWithDomain:@"io.github.sck03.linkoravpn"
                               code:category
                           userInfo:@{NSLocalizedDescriptionKey : message}];
}

static int ValidateCertificate(void *data, const char *reason) {
    PacketTunnelProvider *provider = (__bridge PacketTunnelProvider *)data;
    unsigned char *der = NULL;
    int length = openconnect_get_peer_cert_DER(provider.vpn, &der);
    const unsigned char *cursor = der;
    X509 *certificate = length > 0 ? d2i_X509(NULL, &cursor, length) : NULL;
    BOOL valid = certificate && X509_cmp_current_time(X509_get0_notBefore(certificate)) < 0 &&
                 X509_cmp_current_time(X509_get0_notAfter(certificate)) > 0;
    X509_free(certificate);
    if (der)
        openconnect_free_cert_info(provider.vpn, der);
    if (provider.stopping || !valid ||
        openconnect_check_peer_cert_hash(provider.vpn, provider.pin.UTF8String)) {
        provider.connectionError = VPNError(5, @"证书指纹或有效期验证失败");
        return -1;
    }
    return 0;
}

static int Authenticate(void *data, struct oc_auth_form *form) {
    PacketTunnelProvider *provider = (__bridge PacketTunnelProvider *)data;
    if (provider.stopping)
        return OC_FORM_RESULT_CANCELLED;
    const char *host = openconnect_get_dnsname(provider.vpn);
    NSString *expectedHost = [provider.server.host
        stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"[]"]];
    if (!form || !host || [expectedHost caseInsensitiveCompare:@(host)] != NSOrderedSame ||
        openconnect_get_port(provider.vpn) != (provider.server.port ? provider.server.port.intValue : 443) ||
        (form->error && form->error[0]) || ++provider.authAttempts > 3 ||
        openconnect_check_peer_cert_hash(provider.vpn, provider.pin.UTF8String)) {
        provider.connectionError = VPNError(4, @"认证失败，请核对账号、密码和服务器");
        return OC_FORM_RESULT_CANCELLED;
    }
    [provider recordState:@"authenticating"];
    for (struct oc_form_opt *opt = form->opts; opt; opt = opt->next) {
        if ((opt->flags & OC_FORM_OPT_IGNORE) || opt->type == OC_FORM_OPT_HIDDEN)
            continue;
        const char *value = NULL;
        if (!opt->name)
            return OC_FORM_RESULT_CANCELLED;
        if (opt->type == OC_FORM_OPT_TEXT && !strcmp(opt->name, "username"))
            value = provider.username.UTF8String;
        else if (opt->type == OC_FORM_OPT_PASSWORD && !strcmp(opt->name, "password"))
            value = provider.password.UTF8String;
        else {
            provider.connectionError = VPNError(4, @"服务器要求此客户端未支持的交互认证");
            return OC_FORM_RESULT_CANCELLED; // Never put a password into an unknown/MFA field.
        }
        if (openconnect_set_option_value(opt, value))
            return OC_FORM_RESULT_ERR;
    }
    return OC_FORM_RESULT_OK;
}

static void Progress(void *data, int level, const char *format, ...) {
    // Core messages may contain authentication material. Do not persist or print them.
}

static void Reconnected(void *data) {
    PacketTunnelProvider *provider = (__bridge PacketTunnelProvider *)data;
    if (provider.stopping)
        return;
    provider.reasserting = YES;
    [provider recordState:@"reconnecting"];
    NSError *error = [provider configureTunnel];
    if (error) {
        provider.connectionError = error;
        @synchronized(provider) {
            if (provider.commandFD >= 0) {
                char command = OC_CMD_CANCEL;
                write(provider.commandFD, &command, 1);
            }
        }
    } else {
        provider.reasserting = NO;
        [provider recordState:@"connected"];
    }
}

@implementation PacketTunnelProvider
- (void)recordState:(NSString *)phase {
    @synchronized(self) {
        self.phase = phase;
        self.stateGeneration++;
    }
}

- (void)startTunnelWithOptions:(NSDictionary<NSString *, NSObject *> *)options
             completionHandler:(void (^)(NSError *))completionHandler {
    @synchronized(self) {
        if (self.running) {
            completionHandler(VPNError(9, @"上一个连接尚未结束"));
            return;
        }
        self.running = YES;
        self.stopping = NO;
        self.commandFD = -1;
        self.generation++;
        self.sessionID = NSUUID.UUID.UUIDString;
        self.stateGeneration = 0;
        self.phase = @"connecting";
        self.stopCompletions = [NSMutableArray array];
        self.loopRunning = self.pauseRequested = NO;
    }
    self.sleeping = self.established = NO;
    self.resumed = dispatch_semaphore_create(0);
    self.packetFD = -1;
    self.authAttempts = 0;
    self.connectionError = nil;
    NETunnelProviderProtocol *config = (NETunnelProviderProtocol *)self.protocolConfiguration;
    NSDictionary *values = config.providerConfiguration;
    self.username = [values[@"username"] isKindOfClass:NSString.class] ? values[@"username"] : @"";
    self.pin = [values[@"pin"] isKindOfClass:NSString.class] ? values[@"pin"] : @"";
    self.password =
        [options[@"password"] isKindOfClass:NSString.class] ? (NSString *)options[@"password"] : @"";
    NSURL *server = VPNServerURL(config.serverAddress);
    self.server = server;
    NSData *hash = [self.pin hasPrefix:@"pin-sha256:"]
                       ? [[NSData alloc] initWithBase64EncodedString:[self.pin substringFromIndex:11]
                                                             options:0]
                       : nil;
    if (!server || hash.length != 32 ||
        ![[hash base64EncodedStringWithOptions:0] isEqualToString:[self.pin substringFromIndex:11]] ||
        !self.username.length || !self.password.length) {
        self.password = nil;
        NSArray<dispatch_block_t> *stopped;
        @synchronized(self) {
            self.running = NO;
            stopped = self.stopCompletions.copy;
            self.stopCompletions = nil;
            self.phase = @"failed";
        }
        completionHandler(VPNError(6, @"服务器、账号、密码或指纹无效"));
        for (dispatch_block_t callback in stopped)
            callback();
        return;
    }
    self.packets = dispatch_queue_create("io.github.sck03.linkoravpn.packets", DISPATCH_QUEUE_SERIAL);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
      @autoreleasepool {
          [self runServer:server.absoluteString completion:completionHandler];
      }
    });
}

- (void)runServer:(NSString *)server completion:(void (^)(NSError *))completion {
    NSError *failure = nil;
    BOOL started = NO;
    int coreTunFD = -1;
    int sslResult = openconnect_init_ssl();
    @synchronized(self) {
        if (!self.stopping && !sslResult)
            self.vpn = openconnect_vpninfo_new("LinkoraVPN-Apple", ValidateCertificate, NULL, Authenticate,
                                               Progress, (__bridge void *)self);
        if (self.vpn)
            self.commandFD = openconnect_setup_cmd_pipe(self.vpn);
    }
    do {
        if (self.stopping || !self.vpn || self.commandFD < 0) {
            failure = VPNError(11, @"无法启动 VPN 核心");
            break;
        }
        // The packaged OpenSSL has no iOS root store. Trust only the explicitly supplied pin.
        openconnect_set_system_trust(self.vpn, 0);
        openconnect_set_reqmtu(self.vpn, 1400);
        openconnect_set_dpd(self.vpn, 30);
        openconnect_set_reconnected_handler(self.vpn, Reconnected);
        if (openconnect_set_protocol(self.vpn, "anyconnect") ||
            openconnect_parse_url(self.vpn, server.UTF8String)) {
            failure = VPNError(6, @"服务器或协议配置无效");
            break;
        }
        int authentication = openconnect_obtain_cookie(self.vpn);
        if (authentication || self.stopping) {
            NSInteger category =
                authentication == -ENOENT || authentication == -EPERM || authentication == -EACCES ? 4
                : authentication == -ETIMEDOUT                                                     ? 3
                                                                                                   : 2;
            failure = self.connectionError
                          ?: VPNError(category, category == 4 ? @"认证失败，请检查账号和密码"
                                                              : @"无法连接服务器，请检查网络和地址");
            break;
        }
        self.password = nil;
        if (openconnect_make_cstp_connection(self.vpn)) {
            failure = self.connectionError ?: VPNError(2, @"无法建立 VPN 隧道");
            break;
        }
        failure = [self configureTunnel];
        if (failure || self.stopping)
            break;
        int pair[2];
        if (socketpair(AF_UNIX, SOCK_DGRAM, 0, pair)) {
            failure = VPNError(11, @"无法创建数据通道");
            break;
        }
        // Caller-supplied TUN descriptors without a vpnc script remain caller-owned.
        coreTunFD = pair[0];
        if (openconnect_setup_tun_fd(self.vpn, pair[0])) {
            close(pair[1]);
            failure = VPNError(8, @"无法配置数据通道");
            break;
        }
        int flags = fcntl(pair[1], F_GETFL, 0);
        if (flags < 0 || fcntl(pair[1], F_SETFL, flags | O_NONBLOCK)) {
            close(pair[1]);
            failure = VPNError(8, @"无法配置非阻塞数据通道");
            break;
        }
        int packetFD = pair[1];
        dispatch_sync(self.packets, ^{
          [self startPackets:packetFD];
        });
        // DTLS failure falls back to the already established TLS tunnel.
        openconnect_setup_dtls(self.vpn, 60);
        started = YES;
        self.established = YES;
        [self recordState:@"connected"];
        completion(nil);
        while (!self.stopping) {
            @synchronized(self) {
                self.loopRunning = YES;
            }
            if (self.sleeping)
                [self requestPause];
            int result = openconnect_mainloop(self.vpn, 300, 10);
            @synchronized(self) {
                self.loopRunning = self.pauseRequested = NO;
            }
            if (self.stopping)
                break;
            if (result) {
                NSInteger category = result == -EPERM       ? 4
                                     : result == -ETIMEDOUT ? 3
                                     : result == -EPIPE     ? 10
                                                            : 2;
                failure = self.connectionError ?: VPNError(category, @"VPN 连接中断");
                break;
            }
            [self recordState:self.sleeping ? @"suspended" : @"reconnecting"];
            while (self.sleeping && !self.stopping)
                dispatch_semaphore_wait(self.resumed, DISPATCH_TIME_FOREVER);
            if (!self.stopping) {
                self.reasserting = YES;
                [self recordState:@"reconnecting"];
            }
        }
    } while (NO);
    self.password = nil;
    self.established = NO;
    [self recordState:@"disconnecting"];
    dispatch_sync(self.packets, ^{
      self.packetFD = -1;
      if (self.reader) {
          dispatch_source_cancel(self.reader);
          self.reader = nil;
      }
    });
    if (self.packetClosed) {
        dispatch_semaphore_wait(self.packetClosed, DISPATCH_TIME_FOREVER);
        self.packetClosed = nil;
    }
    NSArray<dispatch_block_t> *stopped;
    BOOL canceled;
    @synchronized(self) {
        self.commandFD = -1;
        if (self.vpn) {
            openconnect_vpninfo_free(self.vpn);
            self.vpn = NULL;
        }
        stopped = self.stopCompletions.copy;
        self.stopCompletions = nil;
        canceled = self.stopping;
    }
    if (coreTunFD >= 0)
        close(coreTunFD);
    [self recordState:canceled ? @"idle" : failure ? @"failed" : @"idle"];
    @synchronized(self) {
        self.running = NO;
    }
    if (!started)
        completion(canceled ? VPNError(1, @"连接已取消") : failure);
    else if (failure && !canceled)
        [self cancelTunnelWithError:failure];
    for (dispatch_block_t callback in stopped)
        callback();
}

- (NSError *)configureTunnel {
    const struct oc_ip_info *info = NULL;
    if (openconnect_get_ip_info(self.vpn, &info, NULL, NULL) || !info || !info->addr || !info->netmask)
        return VPNError(8, @"服务器未分配 IPv4 地址");
    NSString *gateway = info->gateway_addr ? @(info->gateway_addr) : nil;
    if (!gateway.length)
        return VPNError(8, @"服务器地址不可用");
    if (info->mtu < 1280 || info->mtu > 65535)
        return VPNError(8, @"服务器 MTU 无效；全隧道 IPv6 需要至少 1280 字节");
    NEPacketTunnelNetworkSettings *settings =
        [[NEPacketTunnelNetworkSettings alloc] initWithTunnelRemoteAddress:gateway];
    settings.MTU = @(info->mtu);
    settings.IPv4Settings = [[NEIPv4Settings alloc] initWithAddresses:@[ @(info->addr) ]
                                                          subnetMasks:@[ @(info->netmask) ]];
    settings.IPv4Settings.includedRoutes = @[ [NEIPv4Route defaultRoute] ];
    // Capture IPv6 even when the server assigns none, so it cannot bypass the tunnel.
    NSArray<NSString *> *parts = info->netmask6 ? [@(info->netmask6) componentsSeparatedByString:@"/"] : @[];
    NSString *v6 = info->addr6 ? @(info->addr6) : (parts.count == 2 ? parts[0] : @"fd00::2");
    if ([v6 containsString:@"/"])
        v6 = [v6 componentsSeparatedByString:@"/"][0];
    int prefixLength = parts.count == 2 ? vpn_prefix6(parts[1].UTF8String) : 128;
    struct in6_addr parsedV6;
    if (parts.count > 2 || prefixLength < 0 || inet_pton(AF_INET6, v6.UTF8String, &parsedV6) != 1)
        return VPNError(8, @"服务器 IPv6 前缀无效");
    settings.IPv6Settings = [[NEIPv6Settings alloc] initWithAddresses:@[ v6 ]
                                                 networkPrefixLengths:@[ @(prefixLength) ]];
    settings.IPv6Settings.includedRoutes = @[ [NEIPv6Route defaultRoute] ];
    NSMutableArray *dns = [NSMutableArray array];
    for (int i = 0; i < 3; i++)
        if (info->dns[i])
            [dns addObject:@(info->dns[i])];
    if (!dns.count)
        return VPNError(8, @"服务器未提供 DNS");
    settings.DNSSettings = [[NEDNSSettings alloc] initWithServers:dns];
    settings.DNSSettings.matchDomains = @[ @"" ];
    dispatch_semaphore_t ready = dispatch_semaphore_create(0);
    __block NSError *settingsError = nil;
    [self setTunnelNetworkSettings:settings
                 completionHandler:^(NSError *error) {
                   settingsError = error;
                   dispatch_semaphore_signal(ready);
                 }];
    for (unsigned attempt = 0; attempt < 150 && !self.stopping; ++attempt) {
        if (!dispatch_semaphore_wait(ready, dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC)))
            return settingsError;
    }
    return VPNError(self.stopping ? 1 : 3, self.stopping ? @"连接已取消" : @"应用网络设置超时");
}

- (void)startPackets:(int)fd {
    NSUInteger generation = self.generation;
    self.packetClosed = dispatch_semaphore_create(0);
    dispatch_semaphore_t closed = self.packetClosed;
    self.packetFD = fd;
    self.reader = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, fd, 0, self.packets);
    __weak PacketTunnelProvider *weakSelf = self;
    dispatch_source_set_event_handler(self.reader, ^{
      PacketTunnelProvider *provider = weakSelf;
      @synchronized(provider) {
          if (!provider || provider.stopping || provider.generation != generation || provider.packetFD != fd)
              return;
          unsigned char bytes[65540];
          NSMutableArray *packets = [NSMutableArray arrayWithCapacity:32];
          NSMutableArray *protocols = [NSMutableArray arrayWithCapacity:32];
          // Bound each event so disconnect/cleanup cannot starve under sustained traffic.
          for (int i = 0; i < 32 && !provider.stopping; i++) {
              ssize_t count = recv(fd, bytes, sizeof(bytes), 0);
              if (count < 0)
                  break;
              if (count <= 4)
                  continue;
              uint32_t family;
              memcpy(&family, bytes, 4);
              family = ntohl(family);
              if (family != AF_INET && family != AF_INET6)
                  continue;
              if (!vpn_packet_valid(bytes + 4, (size_t)count - 4, family == AF_INET ? 4 : 6))
                  continue;
              NSData *packet = [NSData dataWithBytes:bytes + 4 length:(NSUInteger)count - 4];
              [packets addObject:packet];
              [protocols addObject:@(family)];
          }
          if (packets.count)
              [provider.packetFlow writePackets:packets withProtocols:protocols];
      }
    });
    dispatch_source_set_cancel_handler(self.reader, ^{
      close(fd);
      dispatch_semaphore_signal(closed);
    });
    dispatch_resume(self.reader);
    [self readPackets];
}

- (void)readPackets {
    if (self.stopping || self.packetFD < 0)
        return;
    __weak PacketTunnelProvider *weakSelf = self;
    NSUInteger generation = self.generation;
    dispatch_queue_t queue = self.packets;
    [self.packetFlow
        readPacketsWithCompletionHandler:^(NSArray<NSData *> *packets, NSArray<NSNumber *> *protocols) {
          PacketTunnelProvider *provider = weakSelf;
          if (!provider)
              return;
          dispatch_async(queue, ^{
            @synchronized(provider) {
                if (provider.stopping || provider.generation != generation || provider.packetFD < 0)
                    return;
                if (packets.count != protocols.count) {
                    [provider readPackets];
                    return;
                }
                for (NSUInteger i = 0; i < packets.count; i++) {
                    unsigned protocol = protocols[i].unsignedIntValue;
                    if (protocol != AF_INET && protocol != AF_INET6)
                        continue;
                    if (!vpn_packet_valid(packets[i].bytes, packets[i].length, protocol == AF_INET ? 4 : 6))
                        continue;
                    uint32_t family = htonl(protocols[i].unsignedIntValue);
                    struct iovec vectors[2] = {{&family, sizeof(family)},
                                               {(void *)packets[i].bytes, packets[i].length}};
                    struct msghdr message = {0};
                    message.msg_iov = vectors;
                    message.msg_iovlen = 2;
                    // Darwin OpenConnect expects the four-byte network-order AF prefix.
                    // Drop on backpressure, just as a bounded TUN queue does.
                    sendmsg(provider.packetFD, &message, 0);
                }
                [provider readPackets];
            }
          });
        }];
}

- (void)stopTunnelWithReason:(NEProviderStopReason)reason
           completionHandler:(void (^)(void))completionHandler {
    self.stopping = YES;
    BOOL immediate;
    @synchronized(self) {
        immediate = !self.running;
        if (!immediate) {
            [self.stopCompletions addObject:[completionHandler copy]];
            if (self.commandFD >= 0) {
                char command = OC_CMD_CANCEL;
                write(self.commandFD, &command, 1);
            }
        }
    }
    if (self.resumed)
        dispatch_semaphore_signal(self.resumed);
    if (immediate)
        completionHandler();
}
- (void)sleepWithCompletionHandler:(void (^)(void))completionHandler {
    self.sleeping = YES;
    [self requestPause];
    completionHandler();
}
- (void)wake {
    self.sleeping = NO;
    if (self.resumed)
        dispatch_semaphore_signal(self.resumed);
    [self requestPause];
}
- (void)requestPause {
    @synchronized(self) {
        if (self.established && self.loopRunning && !self.pauseRequested && !self.stopping &&
            self.commandFD >= 0) {
            char command = OC_CMD_PAUSE;
            self.pauseRequested = write(self.commandFD, &command, 1) == 1;
        }
    }
}
- (void)handleAppMessage:(NSData *)messageData completionHandler:(void (^)(NSData *))completionHandler {
    if (!completionHandler)
        return;
    NSString *request = [[NSString alloc] initWithData:messageData encoding:NSUTF8StringEncoding];
    if (![request isEqualToString:@"diagnostics"]) {
        completionHandler(nil);
        return;
    }
    @synchronized(self) {
        NSDictionary *snapshot = @{
            @"session_id" : self.sessionID ?: @"",
            @"generation" : @(self.stateGeneration),
            @"state" : self.phase ?: @"idle"
        };
        completionHandler([NSJSONSerialization dataWithJSONObject:snapshot options:0 error:nil]);
    }
}
@end
