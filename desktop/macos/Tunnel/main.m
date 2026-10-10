#import <NetworkExtension/NetworkExtension.h>

int main(void) {
    @autoreleasepool {
        [NEProvider startSystemExtensionMode];
    }
    dispatch_main();
}
