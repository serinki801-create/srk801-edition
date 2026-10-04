// ============================================================================
// security.c — Stack protector + NX/XD + санитайзер указателей Ring 3
// ============================================================================
#include "security.h"
#include "vga.h"
#include <stdint.h>

#define PTE_P   (1ULL << 0)
#define PTE_W   (1ULL << 1)
#define PTE_PS  (1ULL << 7)
#define PTE_NX  (1ULL << 63)
#define SIZE_2M 0x200000ULL
#define SIZE_4K 0x1000ULL
#define SPLIT_END 0x600000ULL   // сплитим PDT[0..2] (0-6 МБ) сразу
#define MSR_EFER 0xC0000080ULL
#define EFER_NXE (1ULL << 11)

extern char text_start[], text_end[];

// --- Stack protector runtime (для -fstack-protector-strong) ----------------------
// Отдельного TLS/рандома в ядре нет: фиксированный канареечный ключ
// (ядро без ASLR — честно документируем; защита ловит линейные переполнения).
uint64_t __stack_chk_guard = 0x595E9F3B4A7C2D1FULL;

void __stack_chk_fail(void)
{
    __asm__ __volatile__ ("cli");
    vga_print("\n*** STACK SMASHING DETECTED ***\n", 0x4F);
    vga_print("Stack protector: canary mismatch, halting.\n", 0x4F);
    for (;;)
        __asm__ __volatile__ ("hlt");
}

// --- NX-таблицы ----------------------------------------------------------------------
// 3 PT под сплит 0-6 МБ + 4 запасных для on-demand сплита (ELF выше 6 МБ).
static uint64_t nx_pt_base[3][512] __attribute__((aligned(4096)));
static uint64_t nx_pt_extra[4][512] __attribute__((aligned(4096)));
static int nx_extra_used = 0;

static uint64_t g_ustack_lo = 0, g_ustack_hi = 0;

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t v)
{
    __asm__ __volatile__ ("wrmsr" : : "a"((uint32_t)v),
                          "d"((uint32_t)(v >> 32)), "c"(msr));
}

static inline void tlb_flush(void)
{
    __asm__ __volatile__ ("mov %%cr3, %%rax\n\tmov %%rax, %%cr3"
                          : : : "rax", "memory");
}

// PDPT через CR3 (как в framebuffer.c — без правок boot.asm).
static uint64_t *get_pdpt(void)
{
    uint64_t cr3;
    __asm__ __volatile__ ("mov %%cr3, %0" : "=r"(cr3));
    uint64_t pml4e = *(volatile uint64_t *)(uintptr_t)(cr3 & ~0xFFFULL);
    return (uint64_t *)(uintptr_t)(pml4e & ~0xFFFULL);
}

// PDPT[0] указывает на PDТ (4-КБ страница с 512 записями 2-МБ PS-страниц).
static uint64_t *get_pdt(void)
{
    uint64_t *pdpt = get_pdpt();
    uint64_t e = pdpt[0];
    if (!(e & PTE_P))
        return 0;
    return (uint64_t *)(uintptr_t)(e & ~0xFFFULL);
}

void security_init(void)
{
    // 1. Включаем бит NXE в EFER (без него бит NX в PTE — reserved fault).
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_NXE);

    uint64_t tx0 = (uint64_t)(uintptr_t)text_start & ~0xFFFULL;
    uint64_t tx1 = ((uint64_t)(uintptr_t)text_end + 0xFFFULL) & ~0xFFFULL;

    uint64_t *pdt = get_pdt();
    if (!pdt)
        return;

    // 2. Сплит PDT[0..2]: 2-МБ страницы -> 4-КБ PT, NX везде кроме .text.
    for (int i = 0; i < 3; i++) {
        uint64_t base = (uint64_t)i * SIZE_2M;
        for (int j = 0; j < 512; j++) {
            uint64_t pa = base + (uint64_t)j * SIZE_4K;
            int exec = (pa + SIZE_4K > tx0 && pa < tx1);
            nx_pt_base[i][j] = pa | PTE_P | PTE_W | (exec ? 0 : PTE_NX);
        }
        pdt[i] = ((uint64_t)(uintptr_t)nx_pt_base[i] & ~0xFFFULL) | PTE_P | PTE_W;
    }

    // 3. Остальной identity-map (6-512 МБ, 2-МБ страницы) — целиком NX.
    //    Код userland выше 6 МБ получит исполнение через security_set_exec().
    for (int i = 3; i < 256; i++) {
        uint64_t e = pdt[i];
        if (e & PTE_P)
            pdt[i] = e | PTE_NX; // PS-бит сохраняем, добавляем NX
    }

    tlb_flush();

    vga_print("security: NXE on, .text RX, data/stacks NX (0-6M split + rest NX)\n", 0x07);
    vga_print("security: stack protector active, user-ptr sanitizer on\n", 0x07);
}

int security_set_exec(uint64_t addr, uint64_t len, int exec)
{
    if (len == 0 || len > (1ULL << 28))
        return -1;
    if (addr + len < addr)
        return -1;
    uint64_t start = addr & ~0xFFFULL;
    uint64_t end = (addr + len + 0xFFFULL) & ~0xFFFULL;

    uint64_t *pdt = get_pdt();
    if (!pdt)
        return -1;
    int need_flush = 0;

    for (uint64_t pa = start; pa < end; pa += SIZE_4K) {
        if (pa >= SPLIT_END) {
            // Вне сплит-зоны: нужен on-demand сплит 2-МБ страницы.
            uint64_t pdi = (pa >> 21) & 0x1FF;
            uint64_t e = pdt[pdi];
            if (!(e & PTE_P))
                return -1;
            if (e & PTE_PS) {
                if (nx_extra_used >= 4)
                    return -1; // кончились запасные PT
                uint64_t base2m = e & ~0x1FFFFFULL;
                uint64_t *pt = nx_pt_extra[nx_extra_used++];
                for (int j = 0; j < 512; j++)
                    pt[j] = (base2m + (uint64_t)j * SIZE_4K) | PTE_P | PTE_W | PTE_NX;
                pdt[pdi] = ((uint64_t)(uintptr_t)pt & ~0xFFFULL) | PTE_P | PTE_W;
                e = pdt[pdi];
            }
            uint64_t *pt = (uint64_t *)(uintptr_t)(e & ~0xFFFULL);
            uint64_t pti = (pa >> 12) & 0x1FF;
            // Трогаем только свои сплиты (fb-PDT чужой — не трогаем).
            int ours = 0;
            for (int k = 0; k < 4; k++)
                if (pt == nx_pt_extra[k])
                    ours = 1;
            if (!ours)
                return -1;
            if (exec)
                pt[pti] &= ~PTE_NX;
            else
                pt[pti] |= PTE_NX;
            need_flush = 1;
        } else {
            uint64_t pdi = (pa >> 21) & 0x1FF; // 0..2
            int pti = (pa >> 12) & 0x1FF;
            if (exec)
                nx_pt_base[pdi][pti] &= ~PTE_NX;
            else
                nx_pt_base[pdi][pti] |= PTE_NX;
            need_flush = 1;
        }
        if (pa + SIZE_4K < pa)
            break;
    }
    if (need_flush)
        tlb_flush();
    return 0;
}

void security_set_user_stack(uint64_t lo, uint64_t hi)
{
    g_ustack_lo = lo;
    g_ustack_hi = hi;
}

int security_check_user_ptr(uint64_t addr, uint64_t len)
{
    // AUDIT: NULL, переполнение, гигантские длины — сразу отказ.
    if (addr == 0 || len == 0 || len > (1ULL << 20))
        return -1;
    if (addr + len < addr)
        return -1;
    uint64_t end = addr + len;
    // Основная user-область: ELF @0x400000+, user-куча @16 МБ (sys_alloc),
    // всё identity-mapped и вне ядра.
    if (addr >= USER_AREA_START && end <= USER_AREA_END)
        return 0;
    // Текущий стек Ring 3 (лежит в BSS ядра, но это СТЕК пользователя).
    if (g_ustack_hi != 0 && addr >= g_ustack_lo && end <= g_ustack_hi)
        return 0;
    return -1;
}
