// samples/hello.c — хост-сэмпл C (gcc -O2) + встроенная команда `cc` в AI-OS.
#include <stdio.h>
int main(void) {
    int acc = 0;
    for (int i = 1; i <= 100; i++) acc += i;
    printf("hello from C (1..100 = %d)\n", acc);
    return 0;
}
