#import "NetworkSettingsOperation.h"

@implementation VPNNetworkSettingsOperation {
    dispatch_group_t _pending;
    dispatch_semaphore_t _ready;
    NSError *_error;
    BOOL _complete;
}

- (instancetype)initWithGeneration:(NSUInteger)generation pending:(dispatch_group_t)pending {
    self = [super init];
    if (self) {
        _generation = generation;
        _pending = pending;
        _ready = dispatch_semaphore_create(0);
        dispatch_group_enter(_pending);
    }
    return self;
}

- (void)completeWithError:(NSError *)error {
    @synchronized(self) {
        if (_complete)
            return;
        _complete = YES;
        _error = error;
    }
    dispatch_semaphore_signal(_ready);
    dispatch_group_leave(_pending);
}

- (NSError *)error {
    @synchronized(self) {
        return _error;
    }
}

- (BOOL)waitUntil:(dispatch_time_t)deadline {
    return dispatch_semaphore_wait(_ready, deadline) == 0;
}
@end
