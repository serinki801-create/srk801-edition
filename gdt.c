// ============================================================================
// gdt.c — Global Descriptor Table implementation (64-bit Long Mode)
// ============================================================================
//
// Мы создаём GDT вручную, используя массив байтов, чтобы гибко разместить
// обычные дескрипторы (8 байт) и TSS дескриптор (16 байт).
//
// Схема GDT (56 байт, классический порядок):
//   Offset 0x00: Null descriptor (8 байт)
//   Offset 0x08: Kernel 64-bit Code (8 байт) — селектор 0x08
//   Offset 0x10: Kernel Data (8 байт)        — селектор 0x10
//   Offset 0x18: User 64-bit Code (8 байт)  — селектор 0x18 (0x1B с RPL3)
//   Offset 0x20: User Data (8 байт)         — селектор 0x20 (0x23 с RPL3)
//   Offset 0x28: TSS descriptor (16 байт)   — селектор 0x28
// ============================================================================

#include "gdt.h"

// Массив GDT в памяти (40 байт).
static uint8_t gdt[GDT_SIZE] __attribute__((aligned(8)));

// GDTR указатель.
static gdt_ptr_t gdt_ptr;

// TSS (Task State Segment) для 64-битного режима.
// Используется для определения стека при обработке прерываний (RSP0).
struct tss {
    uint32_t reserved1;      // 0x000: Reserved
    uint64_t rsp0;           // 0x004: Ring 0 stack pointer
    uint64_t rsp1;           // 0x00C: Ring 1 stack pointer
    uint64_t rsp2;           // 0x014: Ring 2 stack pointer
    uint64_t reserved2;      // 0x01C: Reserved
    uint64_t ist1;           // 0x024: IST1
    uint64_t ist2;           // 0x02C: IST2
    uint64_t ist3;           // 0x034: IST3
    uint64_t ist4;           // 0x03C: IST4
    uint64_t ist5;           // 0x044: IST5
    uint64_t ist6;           // 0x04C: IST6
    uint64_t ist7;           // 0x054: IST7
    uint64_t reserved3;      // 0x05C: Reserved
    uint16_t reserved4;      // 0x064: Reserved
    uint16_t io_map_base;    // 0x066: I/O permission bitmap base
} __attribute__((packed));

// Статический TSS.
static struct tss tss;

// Объявляем stack_top из boot.asm (определён как global в boot.asm).
extern char stack_top[];

// Инициализация GDT и TSS.
void gdt_init(void)
{
    // --- Создаём TSS ---------------------------------------------------------
    // Заполняем TSS нулями.
    __builtin_memset(&tss, 0, sizeof(tss));

    // RSP0 — стек для Ring 0.
    // При прерывании в Ring 0 CPU загружает RSP из TSS.RSP0.
    // Мы указываем наш kernel stack (stack_top из boot.asm).
    tss.rsp0 = (uint64_t)(uintptr_t)stack_top;

    // I/O permission bitmap: если io_map_base >= размер TSS, bitmap отсутствует.
    // Мы устанавливаем 0xFFFF, чтобы указать отсутствие bitmap.
    tss.io_map_base = 0xFFFF;

    // --- Заполняем GDT -------------------------------------------------------
    // 1. Null descriptor (обязателен, GDT index 0).
    for (int i = 0; i < 8; i++) {
        gdt[i] = 0;
    }

    // 2. Code segment descriptor (GDT index 1, селектор 0x08).
    //    Access: 0x9A = Present(1), DPL=00, S=1, Type=1010 (exec/read)
    //    Granularity: 0x20 = AVL=0, L=1, D/B=0, G=1
    //    Limit = 0 (игнорируется в Long Mode).
    //    Base = 0.
    gdt[0x08] = 0xFF;              // Limit low = 0xFFFF
    gdt[0x09] = 0xFF;
    gdt[0x0A] = 0;                 // Base low = 0
    gdt[0x0B] = 0;
    gdt[0x0C] = 0;                 // Base mid = 0
    gdt[0x0D] = 0x9A;              // Access: present, ring0, code, exec/read
    gdt[0x0E] = 0x20;              // Granularity: L=1, G=1
    gdt[0x0F] = 0;                 // Base high = 0

    // 3. Data segment descriptor (GDT index 2, селектор 0x10).
    //    Access: 0x92 = Present(1), DPL=00, S=1, Type=0010 (data, read/write)
    //    Granularity: 0x00 = AVL=0, L=0, D/B=0, G=0
    gdt[0x10] = 0xFF;              // Limit low = 0xFFFF
    gdt[0x11] = 0xFF;
    gdt[0x12] = 0;                 // Base low = 0
    gdt[0x13] = 0;
    gdt[0x14] = 0;                 // Base mid = 0
    gdt[0x15] = 0x92;              // Access: present, ring0, data, read/write
    gdt[0x16] = 0x00;              // Granularity: L=0, no granularity bit
    gdt[0x17] = 0;                 // Base high = 0

    // 4. User code segment descriptor (GDT index 3, селектор 0x18).
    //    Access: 0xFA = Present(1), DPL=11, S=1, Type=1010 (exec/read)
    //    Granularity: 0x20 = L=1 (64-битный код).
    //    С RPL=3 селектор превращается в 0x1B.
    gdt[0x18] = 0xFF;
    gdt[0x19] = 0xFF;
    gdt[0x1A] = 0;
    gdt[0x1B] = 0;
    gdt[0x1C] = 0;
    gdt[0x1D] = 0xFA;              // present, DPL=3, code, exec/read
    gdt[0x1E] = 0x20;              // L=1
    gdt[0x1F] = 0;

    // 5. User data segment descriptor (GDT index 4, селектор 0x20).
    //    Access: 0xF2 = Present(1), DPL=11, S=1, Type=0010 (read/write).
    //    С RPL=3 селектор превращается в 0x23.
    gdt[0x20] = 0xFF;
    gdt[0x21] = 0xFF;
    gdt[0x22] = 0;
    gdt[0x23] = 0;
    gdt[0x24] = 0;
    gdt[0x25] = 0xF2;              // present, DPL=3, data, read/write
    gdt[0x26] = 0x00;
    gdt[0x27] = 0;

    // 6. TSS descriptor (16 байт, GDT offset 0x28, селектор 0x28).
    //    Access: 0x89 = Present(1), DPL=00, S=0, Type=1001 (available 64-bit TSS)
    //    Для 64-битного TSS: base = 64-битный адрес TSS.
    uint64_t tss_base = (uint64_t)(uintptr_t)&tss;
    uint16_t tss_limit = (uint16_t)(sizeof(tss) - 1);

    gdt[0x28] = (uint8_t)(tss_limit & 0xFF);            // Limit low
    gdt[0x29] = (uint8_t)((tss_limit >> 8) & 0xFF);
    gdt[0x2A] = (uint8_t)(tss_base & 0xFF);            // Base low
    gdt[0x2B] = (uint8_t)((tss_base >> 8) & 0xFF);
    gdt[0x2C] = (uint8_t)((tss_base >> 16) & 0xFF);    // Base mid
    gdt[0x2D] = 0x89;                                  // Access: present, ring0, TSS
    gdt[0x2E] = (uint8_t)((tss_limit >> 16) & 0x0F);   // Limit high + granularity
    gdt[0x2F] = (uint8_t)((tss_base >> 24) & 0xFF);    // Base high

    // Base upper (bits 32-63).
    gdt[0x30] = (uint8_t)((tss_base >> 32) & 0xFF);
    gdt[0x31] = (uint8_t)((tss_base >> 40) & 0xFF);
    gdt[0x32] = (uint8_t)((tss_base >> 48) & 0xFF);
    gdt[0x33] = (uint8_t)((tss_base >> 56) & 0xFF);
    gdt[0x34] = 0;  // Reserved
    gdt[0x35] = 0;
    gdt[0x36] = 0;
    gdt[0x37] = 0;

    // --- Загружаем GDT через LGDT ------------------------------------------------
    gdt_ptr.size = GDT_SIZE - 1;
    gdt_ptr.offset = (uint64_t)(uintptr_t)gdt;

    // LGDT — Load Global Descriptor Table Register.
    // После LGDT CPU использует нашу GDT для всех последующих загрузок селекторов.
    __asm__ __volatile__ ("lgdt %0" : : "m"(gdt_ptr));

    // --- Перезагружаем сегментные регистры -----------------------------------
    // После смены GDT нужно перезагрузить CS, DS, ES, FS, GS, SS.
    // Используем внешнюю функцию из gdt_flush.asm (на NASM),
    // чтобы избежать проблем с GAS far jump.
    extern void gdt_reload(void);
    gdt_reload();

    // --- Загружаем TSS через LTR ------------------------------------------------
    // LTR (Load Task Register) загружает селектор TSS в регистр TR.
    // После LTR CPU использует TSS для загрузки стека при прерываниях (RSP0).
    // Мы загружаем селектор 0x28 (GDT index 5 = TSS descriptor).
    __asm__ __volatile__ ("ltr %%ax" : : "a"((uint16_t)GDT_TSS_SELECTOR));
}

void gdt_set_tss_rsp0(uint64_t rsp0)
{
    tss.rsp0 = rsp0;
}

uint64_t gdt_get_tss_rsp0(void)
{
    return tss.rsp0;
}
