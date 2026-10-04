/*
 * Python-шаблон (MicroPython): приложение-раннер.
 * launcher.m — UIKit-оболочка: запускает встроенный бинарник `python`
 * (кросс-компилированный MicroPython) на main.py через posix_spawn,
 * показывает stdout/stderr в UITextView.
 *
 * Сборка: ipabuild.py build .  (--lang python)
 *   1) кросс-компилирует MicroPython (unix-порт) под arm64-apple-ios
 *   2) кладёт бинарник python + main.py в бандл
 *   3) подписывает оба Mach-O и пакует .ipa
 */
#import <UIKit/UIKit.h>
#import <stdlib.h>
#import <unistd.h>
#import <pthread.h>
#import <stdint.h>
#import <spawn.h>

static UITextView *g_output;

/* Тред-ридер: читает пайп stdout+stderr процесса python в g_output */
static void *reader_thread(void *arg) {
    int fd = (int)(intptr_t)arg;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        char *copy = malloc(n + 1);
        memcpy(copy, buf, n);
        copy[n] = 0;
        dispatch_async(dispatch_get_main_queue(), ^{
            g_output.text = [g_output.text stringByAppendingString:
                [NSString stringWithUTF8String:copy]];
            free(copy);
        });
    }
    close(fd);
    return NULL;
}

static void set_output(NSString *s) {
    dispatch_async(dispatch_get_main_queue(), ^{ g_output.text = s; });
}

static void run_python(void) {
    /* Ищем main.py и бинарник python в каталоге приложения */
    NSString *py = [[NSBundle mainBundle] pathForResource:@"main" ofType:@"py"];
    NSString *bin = [[NSBundle mainBundle] pathForResource:@"python" ofType:nil];
    if (!py || !bin) {
        set_output(@"main.py или бинарник python не найдены в бандле");
        return;
    }

    int pout[2], perr[2];
    pipe(pout); pipe(perr);
    posix_spawn_file_actions_t act;
    posix_spawn_file_actions_init(&act);
    posix_spawn_file_actions_adddup2(&act, pout[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&act, perr[1], STDERR_FILENO);

    char *c_bin = (char *)[bin fileSystemRepresentation];
    char *c_py  = (char *)[py  fileSystemRepresentation];
    char *argv[4] = { c_bin, "python", c_py, 0 };

    pid_t pid;
    int rc = posix_spawnp(&pid, c_bin, &act, 0, argv, NULL);
    if (rc != 0) {
        set_output([NSString stringWithFormat:@"spawn error: %d", rc]);
        return;
    }
    close(pout[1]); close(perr[1]);

    pthread_t t1, t2;
    pthread_create(&t1, 0, reader_thread, (void *)(intptr_t)pout[0]);
    pthread_create(&t2, 0, reader_thread, (void *)(intptr_t)perr[0]);

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(8 * NSEC_PER_SEC)),
        dispatch_get_main_queue(), ^{
            if (kill(pid, 0) == 0) kill(pid, SIGTERM); /* останов после 8 c */
        });
}

@interface PyVC : UIViewController
@end

@implementation PyVC
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor whiteColor];
    g_output = [[UITextView alloc] initWithFrame:
        CGRectMake(0, 120, [UIScreen mainScreen].bounds.size.width, 400)];
    g_output.font = [UIFont monospacedSystemFontOfSize:13 weight:UIFontWeightRegular];
    g_output.editable = NO;
    g_output.text = @"python runner готов\nнажми RUN\n";
    [self.view addSubview:g_output];

    UIButton *btn = [UIButton buttonWithType:UIButtonTypeSystem];
    [btn setTitle:@"RUN" forState:UIControlStateNormal];
    [btn setFrame:CGRectMake(0, 70, [UIScreen mainScreen].bounds.size.width, 44)];
    [btn.titleLabel setFont:[UIFont boldSystemFontOfSize:18]];
    [btn addTarget:self action:@selector(onRun) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:btn];
}
- (void)onRun { [self run_python_bg]; }
- (void)run_python_bg {
    dispatch_async(dispatch_get_global_queue(0, 0), ^{ run_python(); });
}
@end

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow *window;
@end

@implementation AppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)o {
    (void)app; (void)o;
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    self.window.rootViewController = [[PyVC alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
