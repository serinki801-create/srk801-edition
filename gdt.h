// ============================================================================
// gdt.h — Global Descriptor Table for x86_64 Long Mode
// ============================================================================
//
// В 64-битном Long Mode GDT используется для определения сегментов:
//   - Null descriptor (обязателен, индекс 0).
//   - 64-битный код ядра (селектор 0x08, GDT index 1).
//   - 64-битные данные ядра (селектор 0x10, GDT index 2).
//   - 64-битный код пользователя (селектор 0x18, с RPL=3 -> 0x1B).
//   - Данные пользователя (селектор 0x20, с RPL=3 -> 0x23).
//   - TSS descriptor (16 байт, селектор 0x28, GDT index 5).
//
// Типичные селекторы в GDT:
//   0x00 — null
//   0x08 — kernel code (index 1, TI=0, RPL=0)
//   0x10 — kernel data (index 2, TI=0, RPL=0)
//   0x1B — user code (index 3, RPL=3)
//   0x23 — user data (index 4, RPL=3)
//   0x28 — TSS (index 5, TI=0, RPL=0)
//
// GDT entry format (8 bytes):
//   Offset  Size   Description
//   0       2      Limit low (bits 0-15)
//   2       2      Base low (bits 0-15)
//   4       1      Base mid (bits 16-23)
//   5       1      Access byte (Type, S, DPL, P)
//   6       1      Granularity (Limit hi, AVL, L, D/B, G)
//   7       1      Base high (bits 24-31)
//
// TSS entry (16 bytes in 64-bit mode):
//   Same as above for 8 low bytes, then 8 more bytes for Base upper (bits 32-63).
// ============================================================================

#ifndef GDT_H
#define GDT_H

#include <stdint.h>

// Селекторы (смещения в GDT).
#define GDT_NULL_SELECTOR   0x00
#define GDT_CODE_SELECTOR   0x08
#define GDT_DATA_SELECTOR   0x10
#define GDT_USER_CODE_SELECTOR 0x18  // +RPL3 = 0x1B
#define GDT_USER_DATA_SELECTOR 0x20  // +RPL3 = 0x23
#define GDT_TSS_SELECTOR    0x28

// RPL-версии для Ring 3 (селектор | 3).
#define GDT_USER_CODE_RPL3  0x1B
#define GDT_USER_DATA_RPL3  0x23

// Размер GDT: 5 обычных дескрипторов (8 байт) + TSS (16 байт) = 56 байт.
#define GDT_SIZE 56

// Структура GDTR (для инструкции LGDT).
typedef struct {
    uint16_t size;     // Размер GDT в байтах минус 1.
    uint64_t offset;   // 64-битный адрес GDT в памяти.
} __attribute__((packed)) gdt_ptr_t;

// Прототип функции инициализации GDT + TSS.
void gdt_init(void);

// Обновление RSP0 в TSS (стек ядра для переходов Ring3 -> Ring0).
// Используется перед switch_to_user_mode (syscall-стек) и после выхода.
void gdt_set_tss_rsp0(uint64_t rsp0);
uint64_t gdt_get_tss_rsp0(void);

#endif
