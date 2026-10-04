// ============================================================================
// vga.h — VGA Text Mode Driver
// ============================================================================
//
// VGA text mode: 80x25 символов, каждый символ = 2 байта (char + attribute).
// Физический адрес буфера: 0xB8000.
// ============================================================================

#ifndef VGA_H
#define VGA_H

#include <stdint.h>

// Инициализация VGA (очистка экрана, установка курсора).
void vga_init(void);

// Очистка экрана пробелами.
void vga_clear(void);

// Вывод одного символа.
void vga_putchar(char c, uint8_t color);

// Вывод строки.
void vga_print(const char *s, uint8_t color);

// Установка позиции курсора.
void vga_set_cursor(int row, int col);

// Получение текущей позиции курсора.
void vga_get_cursor(int *row, int *col);

// Печать hex/dec чисел (для panic / meminfo / uptime).
void vga_print_hex8(uint8_t v, uint8_t color);
void vga_print_hex32(uint32_t v, uint8_t color);
void vga_print_hex64(uint64_t v, uint8_t color);
void vga_print_u64(uint64_t v, uint8_t color);
void vga_print_dec(int64_t v, uint8_t color);

#endif
