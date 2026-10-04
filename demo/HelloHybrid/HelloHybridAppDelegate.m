#import <UIKit/UIKit.h>
#import <HelloHybrid-Swift.h>

@interface HelloVC : UIViewController
@end

@implementation HelloVC
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor systemYellowColor];

    // Направление ObjC→Swift: Counter приехал из сгенерированного
    // HelloHybrid-Swift.h (этого файла нет в исходниках — его обязан
    // сгенерировать Theos/swiftc при сборке, см. проверку в логе).
    Counter *c = [[Counter alloc] init];
    (void)[c bump];
    (void)[c bump];
    NSString *t = [NSString stringWithFormat:@"%@\nn=%ld answer=%ld",
        [Counter describe], (long)c.n, (long)[Counter answer]];

    UILabel *l = [[UILabel alloc] initWithFrame:CGRectMake(20, 200, 340, 120)];
    l.numberOfLines = 0;
    l.text = t;
    [self.view addSubview:l];
}
@end

@interface HelloHybridAppDelegate : UIResponder <UIApplicationDelegate> {
    UIWindow *_window;
}
@end

@implementation HelloHybridAppDelegate
- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)application; (void)options;
    _window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    _window.rootViewController = [[HelloVC alloc] init];
    [_window makeKeyAndVisible];
    return YES;
}
@end
