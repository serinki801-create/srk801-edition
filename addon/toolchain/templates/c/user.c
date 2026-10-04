#include <stdio.h>
#include <string.h>

/*
 * C-шаблон: ваша логика здесь.
 * Напишите текст результата в g_msg — он появится на экране.
 */

/* объявление из glue.m */
extern char g_msg[512];

static int fib(int n) {
    int a = 0, b = 1;
    for (int i = 0; i < n; i++) { int t = a + b; a = b; b = t; }
    return a;
}

void user_main(void) {
    int n = 20;
    snprintf(g_msg, sizeof(g_msg),
             "Hello from C!\nfib(%d) = %d\nЯдро: C, сборка на Linux Mint", n, fib(n));
}
