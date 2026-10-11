#ifndef VPN_SERVER_ADDRESS_H
#define VPN_SERVER_ADDRESS_H
#import <Foundation/Foundation.h>
#include <arpa/inet.h>
#include <string.h>

// The app and extension use the same validation before any authentication.
static inline BOOL VPNServerPinValid(NSString * _Nullable pin) {
    if (![pin isKindOfClass:NSString.class] || pin.length != 55 || ![pin hasPrefix:@"pin-sha256:"]) return NO;
    NSString *encoded = [pin substringFromIndex:11];
    NSData *hash = [[NSData alloc] initWithBase64EncodedString:encoded options:0];
    return hash.length == 32 && [[hash base64EncodedStringWithOptions:0] isEqualToString:encoded];
}

static inline BOOL VPNCredentialValid(NSString * _Nullable value) {
    if (![value isKindOfClass:NSString.class]) return NO;
    NSUInteger length = [value lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
    const char *bytes = value.UTF8String;
    return length && length <= 8192 && bytes && !memchr(bytes, '\0', length);
}

static inline NSURL * _Nullable VPNServerURL(NSString * _Nullable raw) {
    if (![raw isKindOfClass:NSString.class]) return nil;
    NSString *value = [raw stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    NSMutableCharacterSet *invalid = [NSCharacterSet.whitespaceAndNewlineCharacterSet mutableCopy];
    [invalid formUnionWithCharacterSet:NSCharacterSet.controlCharacterSet];
    [invalid addCharactersInString:@"\\"];
    if (!value.length || [value rangeOfCharacterFromSet:invalid].location != NSNotFound) return nil;
    if ([value rangeOfString:@"://"].location == NSNotFound) value = [@"https://" stringByAppendingString:value];
    NSURLComponents *parts = [NSURLComponents componentsWithString:value];
    if (![parts.scheme.lowercaseString isEqualToString:@"https"] || !parts.host.length ||
        parts.user || parts.password || parts.fragment) return nil;
    NSUInteger start = [value rangeOfString:@"://"].location + 3;
    NSString *authority = [value substringFromIndex:start];
    NSRange end = [authority rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"/?#"]];
    if (end.location != NSNotFound) authority = [authority substringToIndex:end.location];
    if ([authority containsString:@"%"]) return nil;
    NSString *port = nil;
    if ([authority hasPrefix:@"["]) {
        NSRange close = [authority rangeOfString:@"]"];
        if (close.location == NSNotFound) return nil;
        NSString *host = [authority substringWithRange:NSMakeRange(1, close.location - 1)];
        struct in6_addr address;
        if (inet_pton(AF_INET6, host.UTF8String, &address) != 1) return nil;
        NSString *suffix = [authority substringFromIndex:close.location + 1];
        if (suffix.length) {
            if (![suffix hasPrefix:@":"]) return nil;
            port = [suffix substringFromIndex:1];
        }
    } else {
        NSArray<NSString *> *fields = [authority componentsSeparatedByString:@":"];
        if (fields.count > 2 || !fields[0].length) return nil;
        if (fields.count == 2) port = fields[1];
        if ([fields[0] rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789."] invertedSet]].location == NSNotFound) {
            NSArray<NSString *> *octets = [fields[0] componentsSeparatedByString:@"."];
            if (octets.count != 4) return nil;
            for (NSString *octet in octets) {
                if (!octet.length || octet.length > 3 || octet.integerValue > 255 ||
                    (octet.length > 1 && [octet hasPrefix:@"0"])) return nil;
            }
            struct in_addr address;
            if (inet_pton(AF_INET, fields[0].UTF8String, &address) != 1) return nil;
        }
    }
    if (port && (!port.length || port.length > 5 || port.integerValue < 1 || port.integerValue > 65535 ||
        [port rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789"] invertedSet]].location != NSNotFound)) return nil;
    parts.scheme = @"https";
    return parts.URL;
}
#endif
