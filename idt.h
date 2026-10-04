// ============================================================================
// idt.h — Interrupt Descriptor Table for x86_64
// ============================================================================
//
// IDT — таблица дескрипторов прерываний (256 записей по 16 байт = 4096 байт).
// Каждая запись содержит адрес обработчика прерывания (ISR).
//
// В Long Mode дескриптор IDT entry (64-bit interrupt gate, 16 байт):
//   Offset 0-15   : low 16 bits of RIP (адрес обработчика)
//   Selector      : селектор сегмента кода (обычно 0x08, 64-битный код)
//   IST           : Interrupt Stack Table (3 бита, 0 = обычный стек)
//   Type/Attr     : Type (4 бита) + DPL (2 бита) + P (1 бит)
//                   Type = 0xE = 64-bit interrupt gate
//   Offset 16-31  : middle 16 bits of RIP
//   Offset 32-63  : high 32 bits of RIP
//   Reserved      : 32 бита (всегда 0)
//
// Векторы прерываний:
//   0-31  : Исключения CPU (divide by zero, page fault, general protection fault...)
//   32-255: Внешние прерывания (IRQ), программные прерывания (int n)
//
// После настройки PIC (Programmable Interrupt Controller):
//   Master PIC (ports 0x20/0x21): векторы 0x20-0x27 (IRQ0-7)
//   Slave  PIC (ports 0xA0/0xA1): векторы 0x28-0x2F (IRQ8-15)
//
// Клавиатура подключена к Master PIC, IRQ1 -> вектор 0x21.
// ============================================================================

#ifndef IDT_H
#define IDT_H

#include <stdint.h>

// Количество записей в IDT (256 стандартных векторов x86).
#define IDT_ENTRIES 256

// Селектор сегмента кода (64-битный код, GDT index 1, шлюзом 0x08).
#define IDT_KERNEL_CODE_SELECTOR 0x08

// Тип дескриптора: 64-bit interrupt gate (0xE).
#define IDT_TYPE_INTERRUPT_GATE 0xE

// Вектор клавиатуры после перенаправления PIC.
#define IDT_IRQ_KEYBOARD 0x21
#define IDT_IRQ_MASTER_BASE 0x20
#define IDT_IRQ_SLAVE_BASE  0x28

// Структура 64-битного дескриптора IDT entry (16 байт).
typedef struct {
    uint16_t offset_low;   // bits 0-15 of handler address
    uint16_t selector;     // code segment selector (CS)
    uint8_t  ist;          // bits 0-2 = IST, bits 3-7 = reserved (0)
    uint8_t  type_attr;    // bits 0-3 = type, bits 5-7 = DPL, bit 7 = P
    uint16_t offset_mid;   // bits 16-31 of handler address
    uint32_t offset_high;  // bits 32-63 of handler address
    uint32_t zero;         // reserved, must be 0
} __attribute__((packed)) idt_entry_t;

// Структура IDTR (IDT Register) — загружается через LIDT (10 байт).
typedef struct {
    uint16_t size;         // IDT size in bytes minus 1 (4096 - 1 = 4095)
    uintptr_t offset;      // физический адрес IDT
} __attribute__((packed)) idt_ptr_t;

// Прототипы функций.
void idt_init(void);
void idt_set_entry(uint8_t vector, uint64_t handler_addr);
// Вариант с выбором DPL (нужен DPL=3 для int 0x80 из Ring 3).
// type_attr собирается как P=1 | (dpl << 5) | 0xE (64-bit interrupt gate).
void idt_set_entry_dpl(uint8_t vector, uint64_t handler_addr, uint8_t dpl);
void idt_load(void);
void pic_unmask_irq(uint8_t irq);

// Вектор программных прерываний для системных вызовов из Ring 3.
#define IDT_SYSCALL_VECTOR 0x80

// C-обработчик прерываний (вызывается из isr.asm).
void isr_handler(uint64_t vector, uint64_t error_code);

#endif
