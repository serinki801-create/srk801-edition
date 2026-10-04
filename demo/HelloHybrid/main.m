#import <UIKit/UIKit.h>

@interface HelloHybridAppDelegate : UIResponder <UIApplicationDelegate>
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil,
                                 NSStringFromClass([HelloHybridAppDelegate class]));
    }
}
