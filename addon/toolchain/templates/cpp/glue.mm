#import <UIKit/UIKit.h>

/* Общий канал между C++-кодом и UI */
char g_msg[512];

extern "C" void user_main(void);

@interface HostVC : UIViewController
@end

@implementation HostVC
- (void)viewDidLoad {
    [super viewDidLoad];
    user_main();
    self.view.backgroundColor = [UIColor whiteColor];
    UILabel *l = [[UILabel alloc] initWithFrame:CGRectMake(20, 200, 340, 220)];
    l.numberOfLines = 0;
    l.font = [UIFont systemFontOfSize:20];
    l.text = [NSString stringWithUTF8String:g_msg];
    [self.view addSubview:l];
}
@end

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow *window;
@end

@implementation AppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)o {
    (void)app; (void)o;
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    self.window.rootViewController = [[HostVC alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
