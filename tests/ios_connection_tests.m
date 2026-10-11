#import "../client/apple/ServerAddress.h"
#include <stdio.h>

int main(void) {
    @autoreleasepool {
        NSDictionary<NSString *, NSString *> *valid = @{
            @" vpn.example.com:4443/group?q=one%20two ": @"https://vpn.example.com:4443/group?q=one%20two",
            @"[2001:db8::1]:4443": @"https://[2001:db8::1]:4443",
            @"HTTPS://vpn.example.com": @"https://vpn.example.com",
            @"192.168.19.253:4443": @"https://192.168.19.253:4443"
        };
        for (NSString *input in valid) {
            if (![VPNServerURL(input).absoluteString isEqualToString:valid[input]]) {
                fprintf(stderr, "Address normalization failed: %s\n", input.UTF8String); return 1;
            }
        }
        NSArray<NSString *> *invalid = @[@"", @"http://example.com", @"user:pass@example.com",
            @"vpn.example.com:", @"vpn.example.com:0", @"vpn.example.com:65536", @"vpn.example.com:12345678901234567890",
            @"[::1:]", @"2001:db8::1", @"[fe80::1%25en0]", @"vpn.example.com/#fragment",
            @"vpn.example.com\nPassword=x", @"vpn.example.com\\path", @"192.168.1.999", @"127.1", @"192.168.01.1"];
        for (NSString *input in invalid) {
            if (VPNServerURL(input)) { fprintf(stderr, "Invalid server address accepted: %s\n", input.UTF8String); return 1; }
        }
        NSString *encoded = [[NSMutableData dataWithLength:32] base64EncodedStringWithOptions:0];
        NSString *pin = [@"pin-sha256:" stringByAppendingString:encoded];
        if (!VPNServerPinValid(pin) || VPNServerPinValid(nil)) return 1;
        NSArray<NSString *> *invalidPins = @[
            @"", @"sha256:abcd", [pin substringToIndex:54], [pin stringByAppendingString:@"="],
            [pin stringByReplacingCharactersInRange:NSMakeRange(53, 1) withString:@"B"],
            [pin stringByAppendingString:@"\n"], [@"pin-sha256:" stringByAppendingString:[@"A" stringByPaddingToLength:65536 withString:@"A" startingAtIndex:0]]
        ];
        for (NSString *input in invalidPins) {
            if (VPNServerPinValid(input)) { fprintf(stderr, "Invalid server pin accepted\n"); return 1; }
        }
        NSString *boundary = [@"a" stringByPaddingToLength:8192 withString:@"a" startingAtIndex:0];
        NSString *unicodeBoundary = [@"密" stringByPaddingToLength:2730 withString:@"密" startingAtIndex:0];
        unichar nul[] = {'a', 0, 'b'};
        if (!VPNCredentialValid(@" 密码 ") || !VPNCredentialValid(boundary) || !VPNCredentialValid(unicodeBoundary) ||
            VPNCredentialValid(nil) || VPNCredentialValid(@"") ||
            VPNCredentialValid([NSString stringWithCharacters:nul length:3]) ||
            VPNCredentialValid([boundary stringByAppendingString:@"a"]) ||
            VPNCredentialValid([unicodeBoundary stringByAppendingString:@"码"])) {
            fprintf(stderr, "Credential byte limits or NUL validation failed\n"); return 1;
        }
        printf("Apple server address, canonical pin and credential policies passed\n");
    }
    return 0;
}
