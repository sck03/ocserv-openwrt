#import "../client/apple/ServerAddress.h"
#import "../client/apple/NetworkSettingsOperation.h"
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
        dispatch_group_t pending = dispatch_group_create();
        VPNNetworkSettingsOperation *operation = [[VPNNetworkSettingsOperation alloc]
            initWithGeneration:7 pending:pending];
        if (operation.generation != 7 || [operation waitUntil:DISPATCH_TIME_NOW] ||
            dispatch_group_wait(pending, DISPATCH_TIME_NOW) == 0) {
            fprintf(stderr, "Unfinished network settings did not retain the stop barrier\n"); return 1;
        }
        // A caller's timeout or cancellation must leave the real OS operation
        // pending. A late failure then releases the barrier exactly once.
        NSError *failure = [NSError errorWithDomain:@"fixture" code:8 userInfo:nil];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
            [operation completeWithError:failure];
        });
        dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC);
        if (![operation waitUntil:deadline] || operation.error != failure ||
            dispatch_group_wait(pending, deadline) || operation.generation != 7) {
            fprintf(stderr, "Late network settings completion was lost\n"); return 1;
        }
        [operation completeWithError:nil];
        if (operation.error != failure || dispatch_group_wait(pending, DISPATCH_TIME_NOW)) {
            fprintf(stderr, "Duplicate network settings completion changed the outcome\n"); return 1;
        }
        VPNNetworkSettingsOperation *next = [[VPNNetworkSettingsOperation alloc]
            initWithGeneration:8 pending:pending];
        [next completeWithError:nil];
        if (![next waitUntil:DISPATCH_TIME_NOW] || next.error ||
            dispatch_group_wait(pending, DISPATCH_TIME_NOW)) return 1;
        printf("Apple address, credential and asynchronous network settings policies passed\n");
    }
    return 0;
}
