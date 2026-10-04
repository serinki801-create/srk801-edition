// ============================================================================
// user_prog.c — Тестовая программа Ring 3 (ET_EXEC, база 0x400000)
// ============================================================================
// Собирается freestanding без libc (user/user.ld), запускается командой
// ядра `exec user_prog.elf` через ELF-загрузчик в Ring 3 (CS=0x1B).
// Общается с ядром только через int 0x80 (см. ../syscall.h).
// ============================================================================
#include "../syscall.h"
#include <stdint.h>

static void print_u64(uint64_t v)
{
    char buf[21];
    int len = 0;
    if (v == 0) {
        sys_print("0");
        return;
    }
    while (v > 0 && len < 20) {
        buf[len++] = (char)('0' + (v % 10));
        v /= 10;
    }
    // Обратный порядок — печатаем по одному символу.
    char one[2] = {0, 0};
    for (int i = len - 1; i >= 0; i--) {
        one[0] = buf[i];
        sys_print(one);
    }
}

int main(void)
{
    sys_print("\n[user] Hello from Ring 3! CPL=3 user program running.\n");

    // Тест sys_alloc: выделяем, пишем паттерн, проверяем.
    uint8_t *mem = (uint8_t *)sys_alloc(128);
    sys_print("[user] sys_alloc(128) = 0x");
    print_u64((uint64_t)(uintptr_t)mem);
    sys_print("\n");
    if (mem) {
        for (int i = 0; i < 128; i++)
            mem[i] = (uint8_t)(i * 3 + 1);
        int ok = 1;
        for (int i = 0; i < 128; i++)
            if (mem[i] != (uint8_t)(i * 3 + 1))
                ok = 0;
        sys_print(ok ? "[user] heap pattern: OK\n" : "[user] heap pattern: FAIL\n");
    } else {
        sys_print("[user] sys_alloc FAILED\n");
    }

    // Тест sys_read (неблокирующий): опрашиваем ввод.
    sys_print("[user] type something + Enter, or wait 3 sec...\n");
    // Простое ожидание: крутимся, опрашивая таймер через... таймера у юзера
    // нет — опрашиваем ввод ограниченное число итераций.
    char line[64];
    uint64_t got = 0;
    for (volatile int spin = 0; spin < 200000000; spin++) {
        __asm__ __volatile__ ("" ::: "memory");
        got = sys_read(line, sizeof(line));
        if (got > 0)
            break;
    }
    if (got > 0) {
        sys_print("[user] you typed: ");
        sys_print(line);
        sys_print("\n");
    } else {
        sys_print("[user] (no input, continuing)\n");
    }

    sys_print("[user] exiting with code 42.\n");
    sys_exit(42);
    // Сюда не возвращаемся.
    for (;;) {
    }
    return 0;
}
