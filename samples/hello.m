#import <Foundation/Foundation.h>
// samples/hello.m — хост-сэмпл Objective-C (GNUstep) + встроенная команда `objc` в AI-OS.
// Хост-сборка: clang $(gnustep-config --objc-flags) -fblocks -c samples/hello.m
//              clang hello.o $(gnustep-config --base-libs) -lobjc -lBlocksRuntime
int main(void) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSArray *a = [NSArray arrayWithObjects:@"C", @"C++", @"ObjC", @"Swift", nil];
    NSLog(@"hello from ObjC (GNUstep): %@", [a componentsJoinedByString:@", "]);
    [pool drain];
    return 0;
}
