// ============================================================================
// timer.c — PIT 8254 @ 100 Гц
// ============================================================================
// PIT (Programmable Interval Timer), базовая частота 1193180 Гц.
// divisor = 1193180 / HZ. Режим 3 (square wave):
//   outb(0x43, 0x36) -> lo(divisor) -> hi(divisor) в порт 0x40 (канал 0).
// Канал 0 подключён к IRQ0 (вектор 0x20 после PIC remap).
// ============================================================================
#include "timer.h"
#include "idt.h"
#include "port.h"
#include <stdint.h>

#define PIT_BASE_FREQ 1193180u
#define PIT_CMD_PORT  0x43
#define PIT_CH0_PORT  0x40
#define TIMER_VECTOR  0x20

extern void timer_irq_handler(void);

static volatile uint64_t g_ticks = 0;

void timer_tick(void)
{
    g_ticks++;
}

void timer_init(void)
{
    uint32_t divisor = PIT_BASE_FREQ / TIMER_HZ; // 11931 при 100 Гц

    __asm__ __volatile__ ("cli");

    // Устанавливаем обработчик IRQ0.
    idt_set_entry(TIMER_VECTOR, (uint64_t)timer_irq_handler);

    // Канал 0, lo/hi, режим 3 (square wave), binary.
    outb(PIT_CMD_PORT, 0x36);
    outb(PIT_CH0_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0_PORT, (uint8_t)((divisor >> 8) & 0xFF));

    g_ticks = 0;

    // Размаскируем IRQ0 (таймер). IRQ1 (клавиатура) уже открыта в idt_init.
    {
        uint8_t mask = inb(0x21) & (uint8_t)~(1 << 0);
        outb(0x21, mask);
    }

    __asm__ __volatile__ ("sti");
}

uint64_t timer_ticks(void)
{
    return g_ticks;
}

uint64_t uptime_ms(void)
{
    // 1 тик = 10 мс.
    return g_ticks * (1000u / TIMER_HZ);
}

void sleep_ms(uint32_t ms)
{
    uint64_t target = uptime_ms() + ms;
    while (uptime_ms() < target)
        __asm__ __volatile__ ("hlt");
}
