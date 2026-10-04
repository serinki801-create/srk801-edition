// ============================================================================
// syscall.h — Системные вызовы AI-OS (ABI общее для ядра и userland)
// ============================================================================
// ABI int 0x80 (64-bit): EAX = номер, RDI/RSI/RDX = аргументы, RAX = возврат.
// Номера:
//   0 sys_print(const char *str) — печать NUL-строки (макс. 4096)
//   1 sys_read(char *buf, uint64_t max) — неблокирующее чтение строки ввода
//   2 sys_exit(uint64_t code) — завершить программу (не возвращается)
//   3 sys_alloc(uint64_t size) — kmalloc, возвращает адрес (0 = OOM)
// Инлайн-обёртки ниже используют только int $0x80 и годятся для userland
// (ядро их не вызывает). Быстрый путь SYSCALL/SYSRET тоже запрограммирован
// (MSR STAR/LSTAR/FMASK), обработчик syscall_entry ведёт в тот же диспетчер.
// ============================================================================
#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#define SYS_PRINT 0
#define SYS_READ  1
#define SYS_EXIT  2
#define SYS_ALLOC 3

#ifndef KERNEL
// --- Userland-обёртки (int 0x80) ----------------------------------------------
static inline uint64_t sys_print(const char *s)
{
    uint64_t ret;
    __asm__ __volatile__ ("int $0x80"
                          : "=a"(ret)
                          : "a"((uint64_t)SYS_PRINT), "D"(s)
                          : "memory");
    return ret;
}

static inline uint64_t sys_read(char *buf, uint64_t maxlen)
{
    uint64_t ret;
    __asm__ __volatile__ ("int $0x80"
                          : "=a"(ret)
                          : "a"((uint64_t)SYS_READ), "D"(buf), "S"(maxlen)
                          : "memory");
    return ret;
}

static inline uint64_t sys_exit(uint64_t code)
{
    uint64_t ret;
    __asm__ __volatile__ ("int $0x80"
                          : "=a"(ret)
                          : "a"((uint64_t)SYS_EXIT), "D"(code)
                          : "memory");
    return ret;
}

static inline void *sys_alloc(uint64_t size)
{
    uint64_t ret;
    __asm__ __volatile__ ("int $0x80"
                          : "=a"(ret)
                          : "a"((uint64_t)SYS_ALLOC), "D"(size)
                          : "memory");
    return (void *)(uintptr_t)ret;
}
#endif

#ifdef KERNEL
// --- Ядро ----------------------------------------------------------------------
void syscall_init(void);

// C-диспетчер (вызывается из обоих стабов: int 0x80 и syscall_entry).
uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3);

// Запуск кода в Ring 3 с изолированным пользовательским стеком.
// entry = RIP программы, user_stack_top = верх стека Ring 3.
// Не возвращается, пока программа не вызовет sys_exit (тогда возвращается
// в вызывающего с кодом выхода в *exit_code, либо 0 при успехе сохранения).
int switch_to_user_mode(uint64_t entry_rip, uint64_t user_stack_top,
                         uint64_t *exit_code);

// Проверка доступности инструкции SYSCALL (CPUID.80000001:EDX[11]).
int syscall_cpu_supported(void);

#endif

#endif
