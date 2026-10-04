#import <Foundation/Foundation.h>

// Чистый ObjC-хелпер без Swift-типов (иначе циклическая зависимость
// через bridging header). Swift вызывает Greeter.greeting() — это
// направление Swift→ObjC.
@interface Greeter : NSObject
+ (NSString *)greeting;
@end
