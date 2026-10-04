// ============================================================================
// framebuffer.h — Линейный видеобуфер (VBE/GOP через Multiboot2 tag 8)
// ============================================================================
// Парсинг тега framebuffer из Multiboot2: адрес, pitch, width, height, bpp.
// Если буфер лежит выше identity-map (первые 512 МБ) — домэппим диапазон
// 2-МБ страницами через свободную запись PDPT (fb_map()).
// Рисование идёт в back-буфер (двойная буферизация), показ — fb_flip().
// Поддерживаются bpp 32/24/16(565). Цвет везде 0xRRGGBB.
// Без тега (qemu -kernel, текстовый VGA) — fb_available()==0, gui недоступен.
// ============================================================================
#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

void fb_init(void *mb_info);
int fb_available(void);

uint32_t fb_width(void);
uint32_t fb_height(void);
uint32_t fb_bpp(void);
uint64_t fb_addr(void);
uint32_t fb_pitch(void);

// Примитивы (рисуют в back-буфер):
void fb_clear(uint32_t color);
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_draw_char(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg);
void fb_draw_string(const char *s, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg);
// Прозрачный текст (без подложки) — для подписей поверх стекла.
void fb_draw_string_transp(const char *s, uint32_t x, uint32_t y, uint32_t fg);

// --- AA-текст (Darling-шрифт, 8x8 grayscale, font_aa.h) -------------------------
// Сглаженный текст в тех же метриках, что и 8x8: advance 9px, cell 8x8.
// bg-вариант сначала закладывает ячейку фоном (непрозрачный текст).
void fb_draw_string_aa(const char *s, uint32_t x, uint32_t y, uint32_t fg);
void fb_draw_string_aa_bg(const char *s, uint32_t x, uint32_t y,
                          uint32_t fg, uint32_t bg);
// Фальшивый bold: второй проход со сдвигом +1px (как в macOS-заголовках).
void fb_draw_string_aa_bold(const char *s, uint32_t x, uint32_t y, uint32_t fg);

// --- macOS glass / perf-kit (Phase 6, Linux Mint) -------------------------------
// Всё целочисленное, без libm/libc: freestanding-safe, дёшево для слабого CPU.
// alpha: 0 = прозрачно, 255 = непрозрачно.
void fb_blend_pixel(uint32_t x, uint32_t y, uint32_t color, uint8_t alpha);
void fb_fill_rect_alpha(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        uint32_t color, uint8_t alpha);
// Матовое стекло: тонированная заливка + светлая верхняя кромка + тёмный низ.
void fb_glass_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                   uint32_t tint, uint8_t alpha);
// Скруглённые прямоугольники (r = радиус углов, 0 = обычный rect).
void fb_fill_round_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        uint32_t r, uint32_t color);
void fb_fill_round_rect_alpha(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              uint32_t r, uint32_t color, uint8_t alpha);

// Показать back-буфер на экране (один memcpy, без мерцания).
void fb_flip(void);

#endif
