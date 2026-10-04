// ============================================================================
// vga.c — VGA Text Mode Driver
// ============================================================================

#include "vga.h"
#include <stddef.h>

#define VGA_BUFFER ((volatile unsigned short *)0xB8000)
#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define COLOR_WHITE 0x07

static int cursor_row = 0;
static int cursor_col = 0;

void vga_init(void)
{
    vga_clear();
    vga_set_cursor(0, 0);
}

void vga_clear(void)
{
    for (int row = 0; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            VGA_BUFFER[row * VGA_WIDTH + col] = (COLOR_WHITE << 8) | ' ';
        }
    }
    cursor_row = 0;
    cursor_col = 0;
}

void vga_putchar(char c, uint8_t color)
{
    if (c == '\n') {
        cursor_row++;
        cursor_col = 0;
        return;
    }
    if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            VGA_BUFFER[cursor_row * VGA_WIDTH + cursor_col] = (color << 8) | ' ';
        }
        return;
    }
    VGA_BUFFER[cursor_row * VGA_WIDTH + cursor_col] = (color << 8) | (unsigned char)c;
    cursor_col++;
    if (cursor_col >= VGA_WIDTH) {
        cursor_col = 0;
        cursor_row++;
    }
    if (cursor_row >= VGA_HEIGHT) {
        // Скроллинг (пока просто сбрасываем наверх).
        cursor_row = 0;
    }
}

void vga_print(const char *s, uint8_t color)
{
    for (size_t i = 0; s[i] != '\0'; i++) {
        vga_putchar(s[i], color);
    }
}

void vga_set_cursor(int row, int col)
{
    cursor_row = row;
    cursor_col = col;
}

void vga_get_cursor(int *row, int *col)
{
    *row = cursor_row;
    *col = cursor_col;
}

static char hex_digit(unsigned v)
{
    v &= 0xF;
    return (char)(v < 10 ? '0' + v : 'A' + (v - 10));
}

void vga_print_hex8(uint8_t v, uint8_t color)
{
    char buf[2];
    buf[0] = hex_digit(v >> 4);
    buf[1] = hex_digit(v);
    for (int i = 0; i < 2; i++)
        vga_putchar(buf[i], color);
}

void vga_print_hex32(uint32_t v, uint8_t color)
{
    vga_print("0x", color);
    for (int i = 7; i >= 0; i--)
        vga_putchar(hex_digit(v >> (i * 4)), color);
}

void vga_print_hex64(uint64_t v, uint8_t color)
{
    vga_print("0x", color);
    for (int i = 15; i >= 0; i--)
        vga_putchar(hex_digit((unsigned)(v >> (i * 4))), color);
}

void vga_print_u64(uint64_t v, uint8_t color)
{
    char buf[21];
    int len = 0;
    if (v == 0) {
        vga_putchar('0', color);
        return;
    }
    while (v > 0 && len < 20) {
        buf[len++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (int i = len - 1; i >= 0; i--)
        vga_putchar(buf[i], color);
}

void vga_print_dec(int64_t v, uint8_t color)
{
    if (v < 0) {
        vga_putchar('-', color);
        v = -v;
    }
    vga_print_u64((uint64_t)v, color);
}
