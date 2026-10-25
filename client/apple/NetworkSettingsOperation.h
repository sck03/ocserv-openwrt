#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>

// Tracks the actual NetworkExtension completion, independently of a caller's
// timeout or cancellation. The group remains entered until the OS finishes so
// a later connection cannot race an old network-settings request.
@interface VPNNetworkSettingsOperation : NSObject
@property(nonatomic, readonly) NSUInteger generation;
@property(nonatomic, readonly, strong) NSError *error;
- (instancetype)initWithGeneration:(NSUInteger)generation pending:(dispatch_group_t)pending;
- (void)completeWithError:(NSError *)error;
// Only the core worker waits; a successful wait consumes the completion signal.
- (BOOL)waitUntil:(dispatch_time_t)deadline;
@end
