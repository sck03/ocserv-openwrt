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
        printf("iOS server address policy: %lu cases passed\n", (unsigned long)(valid.count + invalid.count));
    }
    return 0;
}
