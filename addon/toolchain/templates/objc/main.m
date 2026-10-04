// Objective-C шаблон: приложение целиком на ObjC + UIKit.
// Ваша логика — в методах ObjCVC / можно добавить файлы .m/.h.

#import <UIKit/UIKit.h>

long long objc_factorial(long long n);

@interface ObjCVC : UIViewController
@end

@implementation ObjCVC
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor systemYellowColor];

    NSMutableParagraphStyle *ps = [[NSMutableParagraphStyle alloc] init];
    ps.lineHeightMultiple = 1.3;

    UILabel *l = [[UILabel alloc] initWithFrame:CGRectMake(20, 200, 340, 240)];
    l.numberOfLines = 0;
    l.font = [UIFont systemFontOfSize:20];
    l.attributedText = [[NSAttributedString alloc] initWithString:
        [NSString stringWithFormat:@"Hello from Objective-C!\n%ld факториал = %lld",
         (long)5L, (long long)objc_factorial(5)]
        attributes:@{NSFontAttributeName: [UIFont systemFontOfSize:20],
                     NSParagraphStyleAttributeName: ps}];
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
    self.window.rootViewController = [[ObjCVC alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

long long objc_factorial(long long n) {
    long long r = 1;
    for (long long i = 2; i <= n; i++) r *= i;
    return r;
}

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
