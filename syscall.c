// ============================================================================
// syscall.c — Системные вызовы: int 0x80 (DPL=3) + SYSCALL MSR + Ring 3 запуск
// ============================================================================
// Поддерживаемый ABI — int 0x80 (проверенный путь для userland).
// Быстрый путь SYSCALL/SYSRET тоже программируется по спеке:
//   EFER.SCE=1, STAR, LSTAR=syscall_entry, FMASK=0x200,
// обработчик ведёт в тот же syscall_dispatch.
// Вход в Ring 3 — через IRETQ-кадр (switch_to_user_raw, user.asm):
// ограничений STAR-раскладки нет, работают честные 0x1B/0x23.
// Выход — sys_exit: user_exit_to_kernel восстанавливает kernel RSP/RBP.
// ============================================================================
#include "syscall.h"
#include "idt.h"
#include "gdt.h"
#include "vga.h"
#include "keyboard.h"
#include "kheap.h"
#include "pmm.h"
#include "security.h"
#include <stdint.h>
#include <stddef.h>

// --- MSR -----------------------------------------------------------------------
#define MSR_EFER  0xC0000080
#define MSR_STAR  0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_FMASK 0xC0000084
#define EFER_SCE  (1u << 0)

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t v)
{
    uint32_t lo = (uint32_t)v, hi = (uint32_t)(v >> 32);
    __asm__ __volatile__ ("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

static int g_syscall_supported = 0;

// Из user.asm.
extern void syscall_int80_handler(void);
extern void syscall_entry(void);
extern void switch_to_user_raw(uint64_t entry_rip, uint64_t user_rsp);
extern void user_exit_to_kernel(void);
extern char syscall_stack_top[];
extern char stack_top[]; // boot-стек ядра

static volatile uint64_t g_user_exit_code = 0;

// --- User heap: отдельная куча в user-области (НЕ kernel-heap!) --------------------
// AUDIT (Phase 5): sys_alloc раньше раздавал блоки kheap с inline-заголовками
// free-list в Ring 3 — пользователь мог подделать заголовки и через kfree
// получить произвольную запись в памяти ядра. Теперь user-куча — отдельный
// 1-МБ регион @16 МБ (вне ядра, внутри санитайзер-разрешённой user-области,
// страницы NX). Свои заголовки — только user-пул, kheap ядра недосягаем.
// Вызывается только из контекста syscall (IF=0), блокировки не нужны.
#define UHEAP_BASE 0x1000000ULL
#define UHEAP_SIZE (1024u * 1024u)

typedef struct ublock {
    uint64_t size;
    int free;
    struct ublock *next;
} ublock_t;

static ublock_t *uheap_head = 0;

static void uheap_init(void)
{
    pmm_reserve_range(UHEAP_BASE, UHEAP_SIZE);
    uheap_head = (ublock_t *)(uintptr_t)UHEAP_BASE;
    uheap_head->size = UHEAP_SIZE - sizeof(ublock_t);
    uheap_head->free = 1;
    uheap_head->next = 0;
}

static uint64_t uheap_alloc(uint64_t size)
{
    if (size == 0)
        return 0;
    size = (size + 7) & ~7ULL;
    ublock_t *cur = uheap_head;
    while (cur) {
        if (cur->free && cur->size >= size) {
            uint64_t rest = cur->size - size;
            if (rest >= sizeof(ublock_t) + 8) {
                ublock_t *nb = (ublock_t *)((uint8_t *)cur + sizeof(ublock_t) + size);
                nb->size = rest - sizeof(ublock_t);
                nb->free = 1;
                nb->next = cur->next;
                cur->size = size;
                cur->next = nb;
            }
            cur->free = 0;
            return (uint64_t)(uintptr_t)((uint8_t *)cur + sizeof(ublock_t));
        }
        cur = cur->next;
    }
    return 0;
}

// --- Реализации sys_* --------------------------------------------------------------
static uint64_t sys_do_print(uint64_t ptr)
{
    // AUDIT: сначала санитайзер (Ring 3 не должен читать память ядра),
    // потом чтение с капом 4096.
    if (security_check_user_ptr(ptr, 4096) != 0) {
        vga_print("[security] sys_print denied: bad user ptr ", 0x0C);
        vga_print_hex64(ptr, 0x0C);
        vga_print("\n", 0x0C);
        return 0;
    }
    const char *s = (const char *)(uintptr_t)ptr;
    uint64_t n = 0;
    while (n < 4096 && s[n] != '\0') {
        vga_putchar(s[n], 0x07);
        n++;
    }
    return n;
}

static uint64_t sys_do_read(uint64_t buf_ptr, uint64_t maxlen)
{
    // Неблокирующее чтение: если строка готова — копируем, иначе 0.
    if (!keyboard_is_input_ready())
        return 0;
    if (maxlen == 0 || maxlen > 4096)
        return 0;
    // AUDIT: буфер назначения обязан быть пользовательским.
    if (security_check_user_ptr(buf_ptr, maxlen) != 0) {
        vga_print("[security] sys_read denied: bad user ptr\n", 0x0C);
        return 0;
    }
    static char tmp[256];
    keyboard_get_input(tmp, sizeof(tmp));
    keyboard_clear_input();
    uint64_t n = 0;
    while (tmp[n] && n + 1 < maxlen) {
        ((char *)(uintptr_t)buf_ptr)[n] = tmp[n];
        n++;
    }
    ((char *)(uintptr_t)buf_ptr)[n] = '\0';
    // Эхо для shell-контекста не делаем (программа сама печатает).
    return n;
}

static uint64_t sys_do_exit(uint64_t code)
{
    g_user_exit_code = code;
    vga_print("\n[user] exit(", 0x07);
    vga_print_u64(code, 0x07);
    vga_print(")\n", 0x07);
    user_exit_to_kernel();
    // Сюда не возвращаемся (user_exit_to_kernel делает RET в user_run).
    for (;;)
        __asm__ __volatile__ ("hlt");
    return 0;
}

static uint64_t sys_do_alloc(uint64_t size)
{
    if (size == 0 || size > (1u << 20))
        return 0;
    // AUDIT: только user-куча (см. uheap_*), kheap ядра недоступен из Ring 3.
    return uheap_alloc(size);
}

// --- Диспетчер ------------------------------------------------------------------------
uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3)
{
    (void)a3;
    switch (num) {
        case SYS_PRINT: return sys_do_print(a1);
        case SYS_READ:  return sys_do_read(a1, a2);
        case SYS_EXIT:  return sys_do_exit(a1);
        case SYS_ALLOC: return sys_do_alloc(a1);
        default:
            vga_print("[syscall] unknown #", 0x0C);
            vga_print_u64(num, 0x0C);
            vga_print("\n", 0x0C);
            return (uint64_t)-1;
    }
}

// --- CPUID: поддержка SYSCALL/SYSRET ------------------------------------------------------
int syscall_cpu_supported(void)
{
    return g_syscall_supported;
}

static int cpuid_has_syscall(void)
{
    uint32_t eax, ebx, ecx, edx;
    __asm__ __volatile__ ("cpuid"
                          : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                          : "a"(0x80000001u));
    return (edx >> 11) & 1;
}

// --- Инициализация --------------------------------------------------------------------------
void syscall_init(void)
{
    __asm__ __volatile__ ("cli");

    // Вектор 0x80 с DPL=3 — единственный способ попасть из Ring 3.
    idt_set_entry_dpl(IDT_SYSCALL_VECTOR, (uint64_t)syscall_int80_handler, 3);

    // AUDIT: отдельная user-куча @16 МБ (вне ядра) + резерв в PMM.
    uheap_init();

    g_syscall_supported = cpuid_has_syscall();
    if (g_syscall_supported) {
        // STAR: [63:48]=0x08 (база SYSRET: CS=0x08+16=0x18|3=0x1B),
        //       [47:32]=0x08 (SYSCALL: CS=0x08, SS=0x10).
        uint64_t star = ((uint64_t)0x08 << 48) | ((uint64_t)0x08 << 32);
        wrmsr(MSR_STAR, star);
        wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
        wrmsr(MSR_FMASK, 0x200); // гасить IF на входе SYSCALL
        wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);
    }

    __asm__ __volatile__ ("sti");

    vga_print("syscalls: int 0x80 DPL=3 ok", 0x07);
    if (g_syscall_supported)
        vga_print(", SYSCALL/SYSRET MSRs programmed\n", 0x07);
    else
        vga_print(", SYSCALL not supported by CPU\n", 0x07);
}

// --- Запуск Ring 3 -------------------------------------------------------------------------------
static void reload_kernel_segments(void)
{
    // После возврата из Ring 3 сегменты ещё пользовательские — чиним.
    __asm__ __volatile__ (
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        : : : "rax", "memory");
}

int switch_to_user_mode(uint64_t entry_rip, uint64_t user_stack_top,
                        uint64_t *exit_code)
{
    uint64_t saved_rsp0 = gdt_get_tss_rsp0();

    // Прерывания из Ring 3 должны падать на отдельный syscall-стек.
    gdt_set_tss_rsp0((uint64_t)(uintptr_t)syscall_stack_top);
    g_user_exit_code = 0;

    // AUDIT: открываем санитайзеру окно user-стека (sys_read пишет туда).
    // Стек 16 КБ, лежит в BSS ядра, но принадлежит пользователю.
    security_set_user_stack(user_stack_top - 16384, user_stack_top);

    __asm__ __volatile__ ("sti");
    switch_to_user_raw(entry_rip, user_stack_top);
    // Возврат сюда — только через sys_exit -> user_exit_to_kernel.
    __asm__ __volatile__ ("cli");

    reload_kernel_segments();
    gdt_set_tss_rsp0(saved_rsp0);
    security_set_user_stack(0, 0); // окно закрыто — stale-доступ запрещён
    __asm__ __volatile__ ("sti");

    if (exit_code)
        *exit_code = g_user_exit_code;
    return 0;
}
