// ============================================================================
// idt.c — Interrupt Descriptor Table initialization and PIC setup
// ============================================================================
//
// Процедура инициализации IDT:
//   1. Устанавливаем обработчики для каждого вектора (0-255).
//      - Векторы 0-31 (исключения CPU): default_exception_handler (остановка).
//      - Векторы 32-47 (IRQ): default_irq_handler (EOI),
//        кроме 0x21 (клавиатура), который использует keyboard_irq_handler.
//      - Векторы 48-255: default_exception_handler.
//   2. Программируем PIC (перенаправление векторов 0x00-0x0F -> 0x20-0x2F).
//   3. Загружаем IDT через LIDT.
//   4. Открываем клавиатуру (IRQ1).
//
// Порты PIC:
//   0x20 — Master PIC command
//   0x21 — Master PIC data (маска IRQ0-7)
//   0xA0 — Slave PIC command
//   0xA1 — Slave PIC data (маска IRQ8-15)
//
// CLI / STI:
//   CLI — запрещает прерывания (IF=0 в RFLAGS).
//   STI — разрешает прерывания (IF=1).
//   При настройке IDT/PIC прерывания должны быть запрещены.
// ============================================================================

#include "idt.h"
#include "port.h"
#include "keyboard.h"
#include <stdint.h>

// Массив дескрипторов IDT (256 записей по 16 байт = 4096 байт).
static idt_entry_t idt[IDT_ENTRIES] __attribute__((aligned(16)));

// IDTR — структура для LIDT.
static idt_ptr_t idt_ptr;

// Внешние ассемблерные обработчики (из isr.asm).
extern void default_exception_handler(void);
extern void default_irq_handler(void);

// Обработчик клавиатуры (из keyboard.c, GCC interrupt attribute).
extern void keyboard_irq_handler(void);

// Функция для установки дескриптора IDT entry.
void idt_set_entry(uint8_t vector, uint64_t handler_addr)
{
    idt_set_entry_dpl(vector, handler_addr, 0);
}

// Вариант с выбором DPL (DPL=3 открывает вектор для вызова из Ring 3).
void idt_set_entry_dpl(uint8_t vector, uint64_t handler_addr, uint8_t dpl)
{
    idt[vector].offset_low   = (uint16_t)(handler_addr & 0xFFFF);
    idt[vector].selector     = IDT_KERNEL_CODE_SELECTOR; // 0x08
    idt[vector].ist          = 0;                         // IST = 0 (обычный стек)
    idt[vector].type_attr    = (uint8_t)(0x80 | ((dpl & 0x3) << 5) | 0x0E);
    idt[vector].offset_mid   = (uint16_t)((handler_addr >> 16) & 0xFFFF);
    idt[vector].offset_high  = (uint32_t)((handler_addr >> 32) & 0xFFFFFFFF);
    idt[vector].zero         = 0;
}

// Загрузка IDT через LIDT.
void idt_load(void)
{
    idt_ptr.size   = (uint16_t)(sizeof(idt_entry_t) * IDT_ENTRIES - 1); // 4095
    idt_ptr.offset = (uintptr_t)&idt;

    // LIDT — Load Interrupt Descriptor Table Register.
    // После LIDT CPU использует нашу IDT для всех последующих прерываний.
    __asm__ __volatile__ ("lidt %0" : : "m"(idt_ptr));
}

// Программирование PIC (перенаправление IRQ на вектора 0x20-0x2F).
void pic_remap(void)
{
    // --- Магистральный PIC (Master, ports 0x20/0x21) -------------------------
    // ICW1: 0x11 = требуется ICW4, cascade mode, edge-triggered, 8-битный режим.
    outb(0x20, 0x11);

    // ICW2: 0x20 = базовый вектор для магистрального PIC.
    // IRQ0 -> 0x20, IRQ1 -> 0x21, IRQ2 -> 0x22, ...
    outb(0x21, IDT_IRQ_MASTER_BASE);

    // ICW3: 0x04 = подчинённый PIC подключен к линии IRQ2 магистрального PIC.
    outb(0x21, 0x04);

    // ICW4: 0x01 = 8086/88 mode, не буферизованный, нормальный nested режим.
    outb(0x21, 0x01);

    // --- Подчинённый PIC (Slave, ports 0xA0/0xA1) ----------------------------
    outb(0xA0, 0x11);
    outb(0xA1, IDT_IRQ_SLAVE_BASE);  // Базовый вектор 0x28.
    outb(0xA1, 0x02);                // Подчинённый подключен к IRQ2 магистрального.
    outb(0xA1, 0x01);                // 8086 mode.

    // --- Маски (все прерывания запрещены, откроем позже) ---------------------
    // 0xFF = все биты маски установлены (все IRQ заблокированы).
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

// Снятие маски конкретного IRQ.
void pic_unmask_irq(uint8_t irq)
{
    if (irq < 8) {
        // Магистральный PIC (IRQ0-7).
        uint8_t mask = inb(0x21) & ~(1 << irq);
        outb(0x21, mask);
    } else {
        // Подчинённый PIC (IRQ8-15).
        uint8_t mask = inb(0xA1) & ~(1 << (irq - 8));
        outb(0xA1, mask);
    }
}

// Инициализация IDT и PIC.
void idt_init(void)
{
    // Запрещаем прерывания на время настройки (CLI — Clear Interrupt Flag).
    __asm__ __volatile__ ("cli");

    // Программируем PIC.
    pic_remap();

    // Устанавливаем обработчики для всех векторов.
    for (int i = 0; i < IDT_ENTRIES; i++) {
        if (i == IDT_IRQ_KEYBOARD) {
            // Клавиатура (IRQ1 -> вектор 0x21).
            idt_set_entry(i, (uint64_t)keyboard_irq_handler);
        } else if (i >= IDT_IRQ_MASTER_BASE && i < IDT_IRQ_SLAVE_BASE + 8) {
            // Остальные IRQ — общий обработчик с EOI.
            idt_set_entry(i, (uint64_t)default_irq_handler);
        } else if (i < 32) {
            // Исключения CPU (векторы 0-31) — останавливаем систему.
            idt_set_entry(i, (uint64_t)default_exception_handler);
        } else {
            // Прочие векторы — заглушка на исключения.
            idt_set_entry(i, (uint64_t)default_exception_handler);
        }
    }

    // Загружаем IDT.
    idt_load();

    // Открываем клавиатуру (IRQ1).
    pic_unmask_irq(1);

    // Разрешаем прерывания (STI — Set Interrupt Flag).
    __asm__ __volatile__ ("sti");
}
