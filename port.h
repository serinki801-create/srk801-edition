// ============================================================================
// port.h — Low-level I/O port access (inb / outb)
// ============================================================================
//
// Функции для чтения/записи байта в порт ввода-вывода.
// Используют встроенный ассемблер GCC (AT&T синтаксис).
//
// INB  — чтение байта из порта.
// OUTB — запись байта в порт.
// ============================================================================

#ifndef PORT_H
#define PORT_H

#include <stdint.h>

// Чтение байта из порта.
static inline uint8_t inb(uint16_t port)
{
    uint8_t result;
    __asm__ __volatile__ ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

// Запись байта в порт.
static inline void outb(uint16_t port, uint8_t data)
{
    __asm__ __volatile__ ("outb %0, %1" : : "a"(data), "Nd"(port));
}

#endif
