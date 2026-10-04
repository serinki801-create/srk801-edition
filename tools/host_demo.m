#import <Foundation/Foundation.h>
#include <Block.h>
#include <stdio.h>

// ============================================================================
// host_demo.m — хост-демо Objective-C + Blocks + Foundation под GNUstep.
// Собирается СТРОГО флагами из ТЗ:
//   clang $(gnustep-config --objc-flags) -fblocks -c tools/host_demo.m
//   clang host_demo.o $(gnustep-config --base-libs) -lobjc -lBlocksRuntime
// Проверяет на Linux Mint: Foundation, Blocks, синтез свойств, ARC-less MRR.
// ============================================================================

@interface GlassCard : NSObject {
    NSString *_title;
    double _opacity;
}
@property (nonatomic, copy) NSString *title;
@property (nonatomic) double opacity;
- (NSString *)renderWithBlur:(BOOL)blur;
@end

@implementation GlassCard
@synthesize title = _title, opacity = _opacity;

- (NSString *)renderWithBlur:(BOOL)blur {
    return [NSString stringWithFormat:@"[GlassCard title='%@' opacity=%.2f blur=%@]",
            _title, _opacity, blur ? @"YES" : @"NO"];
}
- (void)dealloc {
    [_title release];
    [super dealloc];
}
@end

int main(int argc, const char *argv[]) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    printf("[host_demo] AI-OS host toolchain: GNUstep + BlocksRuntime on Linux Mint\n");

    // 1. Foundation: строки, массивы, словарь.
    NSArray *langs = [NSArray arrayWithObjects:@"C", @"C++", @"Objective-C", @"Swift", nil];
    printf("[host_demo] langs=%s\n", [[langs componentsJoinedByString:@", "] UTF8String]);
    NSDictionary *info = [NSDictionary dictionaryWithObjectsAndKeys:
                          @"AI-OS", @"os",
                          @"linuxmint", @"host",
                          [NSNumber numberWithInt:4], @"toolchains", nil];
    printf("[host_demo] info=%s\n", [[info description] UTF8String]);

    // 2. Blocks (требует -fblocks + -lBlocksRuntime).
    int (^mul)(int, int) = ^(int a, int b) { return a * b; };
    __block int acc = 0;
    void (^add)(int) = ^(int v) { acc += v; };
    for (int i = 1; i <= 5; i++) add(i);
    printf("[host_demo] block mul(6,7)=%d acc(1..5)=%d\n", mul(6, 7), acc);

    // 3. Класс в стиле macOS glass-визуала ядра (wm.c).
    GlassCard *card = [[GlassCard alloc] init];
    card.title = @"macOS Glass — full transparency";
    card.opacity = 0.65;
    printf("[host_demo] %s\n", [[card renderWithBlur:YES] UTF8String]);
    [card release];

    // 4. NSFileManager — доказательство POSIX/GNUstep вместо Cocoa.
    NSString *cwd = [[NSFileManager defaultManager] currentDirectoryPath];
    printf("[host_demo] cwd=%s\n", [cwd UTF8String]);

    printf("[host_demo] ALL HOST CHECKS PASSED\n");
    [pool drain];
    return 0;
}
