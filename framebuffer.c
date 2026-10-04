// ============================================================================
// framebuffer.c — VBE framebuffer: парсинг MB2, on-demand paging, 2D-примитивы
// ============================================================================
#include "framebuffer.h"
#include "font.h"
#include "font_aa.h"
#include <stdint.h>

#define IDENTITY_LIMIT 0x20000000ULL // 512 МБ (конец boot identity-map)
#define PAGE_2M 0x200000ULL
#define FB_MAX_W 1024
#define FB_MAX_H 768

static int g_present = 0;
static uint64_t g_addr = 0;
static uint32_t g_pitch = 0;
static uint32_t g_w = 0, g_h = 0;
static uint32_t g_bpp = 0;
static uint8_t g_fb_type = 0;

// Back-буфер под максимум 1024x768x32 (3 МБ, NOBITS — файл не растёт).
static uint8_t g_back[FB_MAX_W * FB_MAX_H * 4] __attribute__((aligned(4096)));

// Своя PDT для домэппинга высоких регионов (фреймбуфер QEMU ~0xE0000000).
static uint64_t g_fb_pdt[512] __attribute__((aligned(4096)));

static inline uint64_t fb_back_size(void)
{
    uint64_t bpp_bytes = (g_bpp + 7) / 8;
    if (bpp_bytes == 0)
        bpp_bytes = 4;
    return (uint64_t)g_w * g_h * bpp_bytes;
}

// --- On-demand mapping: [base, base+len) 2-МБ страницами ---------------------------
static void fb_map_range(uint64_t base, uint64_t len)
{
    if (len == 0)
        return;
    uint64_t start = base & ~(PAGE_2M - 1);
    uint64_t end = (base + len + PAGE_2M - 1) & ~(PAGE_2M - 1);

    // PDPT находим через CR3 (без правок boot.asm): CR3 -> PML4[0] -> PDPT.
    uint64_t cr3, pml4e;
    __asm__ __volatile__ ("mov %%cr3, %0" : "=r"(cr3));
    pml4e = *(volatile uint64_t *)(uintptr_t)(cr3 & ~0xFFFULL);
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4e & ~0xFFFULL);

    // Заполняем свою PDT страницами диапазона.
    // AUDIT (Phase 5): видеопамять — данные, ставим NX (бит 63).
    for (uint64_t a = start; a < end; a += PAGE_2M) {
        uint64_t pdi = (a >> 21) & 0x1FF;
        g_fb_pdt[pdi] = a | 0x83ULL | (1ULL << 63); // P|W|PS|NX
    }
    // Линкуем нужные записи PDPT (по 1 ГБ каждая).
    for (uint64_t a = start; a < end; a += 0x40000000ULL) {
        uint64_t pdpi = (a >> 30) & 0x1FF;
        pdpt[pdpi] = ((uint64_t)(uintptr_t)g_fb_pdt & ~0xFFFULL) | 0x03ULL;
    }
    // Сброс TLB перезагрузкой CR3.
    __asm__ __volatile__ ("mov %%cr3, %%rax\n\tmov %%rax, %%cr3" : : : "rax", "memory");
}

// --- Парсинг тега framebuffer (type 8) ------------------------------------------------
// Формат: u32 type, u32 size, u64 addr, u32 pitch, u32 w, u32 h,
//         u8 bpp, u8 type, u16 reserved. Нужен type==1 (packed RGB).
void fb_init(void *mb_info)
{
    g_present = 0;
    if (!mb_info)
        return;
    uint8_t *ptr = (uint8_t *)mb_info;
    uint64_t p = (uint64_t)(uintptr_t)ptr;
    if (p < 0x1000 || p >= 0x20000000)
        return;
    uint32_t total = *(uint32_t *)(ptr + 0);
    if (total < 8 || total >= (1u << 20))
        return;

    uint8_t *tag = ptr + 8;
    uint8_t *end = ptr + total;
    while (tag + 8 <= end) {
        uint32_t type = *(uint32_t *)(tag + 0);
        uint32_t size = *(uint32_t *)(tag + 4);
        if (size < 8)
            break;
        if (type == 0)
            break;
        if (type == 8 && size >= 36) {
            uint64_t addr = *(uint64_t *)(tag + 8);
            uint32_t pitch = *(uint32_t *)(tag + 16);
            uint32_t w = *(uint32_t *)(tag + 20);
            uint32_t h = *(uint32_t *)(tag + 24);
            uint8_t bpp = *(tag + 28);
            uint8_t fbt = *(tag + 29);
            if (fbt == 1 && addr != 0 && w != 0 && h != 0 &&
                (bpp == 32 || bpp == 24 || bpp == 16) &&
                w <= 4096 && h <= 4096) {
                g_addr = addr;
                g_pitch = pitch ? pitch : w * ((bpp + 7) / 8);
                g_w = w;
                g_h = h;
                g_bpp = bpp;
                g_fb_type = fbt;
                // Домэппим, если выше identity-map.
                if (addr + (uint64_t)pitch * h > IDENTITY_LIMIT)
                    fb_map_range(addr, (uint64_t)pitch * h);
                g_present = 1;
            }
            break;
        }
        uint32_t step = (size + 7) & ~7u;
        if (step == 0)
            break;
        tag += step;
        if (tag >= end)
            break;
    }
}

int fb_available(void) { return g_present; }
uint32_t fb_width(void)  { return g_w; }
uint32_t fb_height(void) { return g_h; }
uint32_t fb_bpp(void)    { return g_bpp; }
uint64_t fb_addr(void)   { return g_addr; }
uint32_t fb_pitch(void)  { return g_pitch; }

// --- Запись пикселя в back-буфер ----------------------------------------------------------
// Быстрые пути под каждый bpp: указатель строки вычисляется один раз,
// никаких вызовов/ветвлений на пиксель (важно для слабого CPU).
static void back_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!g_present || x >= g_w || y >= g_h)
        return;
    uint8_t r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    if (g_w > FB_MAX_W || g_h > FB_MAX_H)
        return; // больше back-буфера — не рисуем
    if (g_bpp == 32) {
        uint32_t *row = (uint32_t *)(g_back + (uint64_t)y * g_w * 4);
        row[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    } else if (g_bpp == 24) {
        uint8_t *px = g_back + ((uint64_t)y * g_w + x) * 3;
        px[0] = b; px[1] = g; px[2] = r; // BGR
    } else { // 16 (RGB565)
        uint16_t *row = (uint16_t *)(g_back + (uint64_t)y * g_w * 2);
        row[x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
}

// Чтение пикселя back-буфера как 0xRRGGBB (для альфа-смешивания).
static uint32_t back_get(uint32_t x, uint32_t y)
{
    if (g_bpp == 32) {
        uint32_t *row = (uint32_t *)(g_back + (uint64_t)y * g_w * 4);
        return row[x] & 0xFFFFFFu;
    } else if (g_bpp == 24) {
        uint8_t *px = g_back + ((uint64_t)y * g_w + x) * 3;
        return ((uint32_t)px[2] << 16) | ((uint32_t)px[1] << 8) | px[0];
    } else { // RGB565 -> развернуть в 8 бит на канал
        uint16_t *row = (uint16_t *)(g_back + (uint64_t)y * g_w * 2);
        uint16_t v = row[x];
        uint32_t r = (uint32_t)(v >> 11) & 0x1F;
        uint32_t g = (uint32_t)(v >> 5) & 0x3F;
        uint32_t b = (uint32_t)v & 0x1F;
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        return (r << 16) | (g << 8) | b;
    }
}

// Целочисленное смешивание fg поверх bg (alpha 0..255), без float/libm.
static uint32_t blend32(uint32_t bg, uint32_t fg, uint32_t alpha)
{
    uint32_t sr = (fg >> 16) & 0xFF, sg = (fg >> 8) & 0xFF, sb = fg & 0xFF;
    uint32_t dr = (bg >> 16) & 0xFF, dg = (bg >> 8) & 0xFF, db = bg & 0xFF;
    uint32_t inv = 255 - alpha;
    uint32_t r = (sr * alpha + dr * inv + 127) / 255;
    uint32_t g = (sg * alpha + dg * inv + 127) / 255;
    uint32_t b = (sb * alpha + db * inv + 127) / 255;
    return (r << 16) | (g << 8) | b;
}

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    back_pixel(x, y, color);
}

void fb_blend_pixel(uint32_t x, uint32_t y, uint32_t color, uint8_t alpha)
{
    if (!g_present || x >= g_w || y >= g_h || alpha == 0)
        return;
    if (g_w > FB_MAX_W || g_h > FB_MAX_H)
        return;
    if (alpha == 255) {
        back_pixel(x, y, color);
        return;
    }
    back_pixel(x, y, blend32(back_get(x, y), color, alpha));
}

void fb_clear(uint32_t color)
{
    if (!g_present)
        return;
    // Быстрая заливка через fill_rect на весь экран.
    fb_fill_rect(0, 0, g_w, g_h, color);
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (!g_present)
        return;
    if (x >= g_w || y >= g_h)
        return;
    if (x + w > g_w)
        w = g_w - x;
    if (y + h > g_h)
        h = g_h - y;
    if (w == 0 || h == 0)
        return;
    if (g_w > FB_MAX_W || g_h > FB_MAX_H)
        return;
    uint8_t r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    if (g_bpp == 32) {
        // Один 32-битный стор на пиксель, указатель строки — раз на строку.
        uint32_t px = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        for (uint32_t j = 0; j < h; j++) {
            uint32_t *row = (uint32_t *)(g_back + (uint64_t)(y + j) * g_w * 4) + x;
            for (uint32_t i = 0; i < w; i++)
                row[i] = px;
        }
    } else if (g_bpp == 24) {
        for (uint32_t j = 0; j < h; j++) {
            uint8_t *row = g_back + ((uint64_t)(y + j) * g_w + x) * 3;
            for (uint32_t i = 0; i < w; i++) {
                row[0] = b; row[1] = g; row[2] = r;
                row += 3;
            }
        }
    } else { // 16: один 16-битный стор на пиксель
        uint16_t px = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        for (uint32_t j = 0; j < h; j++) {
            uint16_t *row = (uint16_t *)(g_back + (uint64_t)(y + j) * g_w * 2) + x;
            for (uint32_t i = 0; i < w; i++)
                row[i] = px;
        }
    }
}

void fb_fill_rect_alpha(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        uint32_t color, uint8_t alpha)
{
    if (!g_present || alpha == 0)
        return;
    if (x >= g_w || y >= g_h)
        return;
    if (x + w > g_w)
        w = g_w - x;
    if (y + h > g_h)
        h = g_h - y;
    if (w == 0 || h == 0)
        return;
    if (g_w > FB_MAX_W || g_h > FB_MAX_H)
        return;
    if (alpha == 255) {
        fb_fill_rect(x, y, w, h, color);
        return;
    }
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++) {
            uint32_t xx = x + i, yy = y + j;
            back_pixel(xx, yy, blend32(back_get(xx, yy), color, alpha));
        }
}

// Матовое стекло macOS: тонированная полупрозрачная панель со светлым верхом.
// Дешёвая имитация blur: фон просвечивает через tint, кромки дают объём.
// Один проход, без семплирования соседей — тянет даже Atom 2012 года.
void fb_glass_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                   uint32_t tint, uint8_t alpha)
{
    if (!g_present || w == 0 || h == 0)
        return;
    fb_fill_rect_alpha(x, y, w, h, tint, alpha);
    // Верхний блик (светлая кромка 1px) и нижняя тень (тёмная кромка 1px).
    if (h >= 2) {
        fb_fill_rect_alpha(x, y, w, 1, 0xFFFFFF, 90);
        fb_fill_rect_alpha(x, y + h - 1, w, 1, 0x000000, 70);
    }
}

// Принадлежность пикселя скруглённому углу (целая арифметика, без sqrt).
static int round_inside(uint32_t px, uint32_t py, uint32_t x, uint32_t y,
                        uint32_t w, uint32_t h, uint32_t r)
{
    if (r == 0)
        return 1;
    // Центр картинки — всегда внутри; проверяем только 4 угловых квадрата r*r.
    uint32_t cx = px, cy = py;
    if (px < x + r && py < y + r) {
        cx = x + r - 1 - px; cy = y + r - 1 - py; // левый верхний
    } else if (px >= x + w - r && py < y + r) {
        cx = px - (x + w - r); cy = y + r - 1 - py; // правый верхний
    } else if (px < x + r && py >= y + h - r) {
        cx = x + r - 1 - px; cy = py - (y + h - r); // левый нижний
    } else if (px >= x + w - r && py >= y + h - r) {
        cx = px - (x + w - r); cy = py - (y + h - r); // правый нижний
    } else {
        return 1;
    }
    return cx * cx + cy * cy <= r * r;
}

void fb_fill_round_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        uint32_t r, uint32_t color)
{
    if (!g_present || w == 0 || h == 0)
        return;
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;
    // Быстрые целые полосы: середина — сплошным rect, углы — с маской.
    if (r < h)
        fb_fill_rect(x, y + r, w, h - 2 * r, color);
    if (r < w) {
        fb_fill_rect(x + r, y, w - 2 * r, r, color);
        fb_fill_rect(x + r, y + h - r, w - 2 * r, r, color);
    }
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++) {
            int edge = (i < r || i >= w - r) && (j < r || j >= h - r);
            if (!edge)
                continue;
            if (round_inside(x + i, y + j, x, y, w, h, r))
                back_pixel(x + i, y + j, color);
        }
}

void fb_fill_round_rect_alpha(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              uint32_t r, uint32_t color, uint8_t alpha)
{
    if (!g_present || w == 0 || h == 0 || alpha == 0)
        return;
    if (alpha == 255) {
        fb_fill_round_rect(x, y, w, h, r, color);
        return;
    }
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++) {
            if (!round_inside(x + i, y + j, x, y, w, h, r))
                continue;
            uint32_t xx = x + i, yy = y + j;
            if (xx >= g_w || yy >= g_h)
                continue;
            back_pixel(xx, yy, blend32(back_get(xx, yy), color, alpha));
        }
}

void fb_draw_char(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg)
{
    if (!g_present)
        return;
    unsigned char uc = (unsigned char)c;
    const uint8_t *glyph;
    if (uc >= FONT_FIRST && uc < FONT_FIRST + FONT_COUNT)
        glyph = font8x8[uc - FONT_FIRST];
    else
        glyph = font8x8['?' - FONT_FIRST];
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            uint32_t colr = (bits & (0x80 >> col)) ? fg : bg;
            back_pixel(x + col, y + row, colr);
        }
    }
}

void fb_draw_string(const char *s, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg)
{
    if (!g_present || !s)
        return;
    uint32_t cx = x;
    while (*s) {
        if (*s == '\n') {
            cx = x;
            y += FONT_H + 2;
        } else {
            fb_draw_char(*s, cx, y, fg, bg);
            cx += FONT_W + 1;
        }
        s++;
    }
}

// Прозрачная строка: рисуем только пиксели глифа, фон просвечивает.
// Нужна для подписей на стекле (menu bar, title bar, dock).
void fb_draw_string_transp(const char *s, uint32_t x, uint32_t y, uint32_t fg)
{
    if (!g_present || !s)
        return;
    uint32_t cx = x;
    while (*s) {
        if (*s == '\n') {
            cx = x;
            y += FONT_H + 2;
        } else {
            unsigned char uc = (unsigned char)*s;
            const uint8_t *glyph;
            if (uc >= FONT_FIRST && uc < FONT_FIRST + FONT_COUNT)
                glyph = font8x8[uc - FONT_FIRST];
            else
                glyph = font8x8['?' - FONT_FIRST];
            for (int row = 0; row < FONT_H; row++) {
                uint8_t bits = glyph[row];
                for (int col = 0; col < FONT_W; col++) {
                    if (bits & (0x80 >> col))
                        back_pixel(cx + col, y + row, fg);
                }
            }
            cx += FONT_W + 1;
        }
        s++;
    }
}

// --- AA-текст (Darling-шрифт): 8x8 grayscale-глифы, бленд fg поверх фона --------
// a — накрытие глифа 0..255. dst = fg*a + cur*(255-a) (те же формулы, что blend32).
static void aa_put(uint32_t x, uint32_t y, uint32_t fg, uint8_t a)
{
    if (a == 0)
        return;
    if (a == 255) {
        back_pixel(x, y, fg);
        return;
    }
    back_pixel(x, y, blend32(back_get(x, y), fg, a));
}

static void aa_glyph(char c, uint32_t x, uint32_t y, uint32_t fg)
{
    unsigned char uc = (unsigned char)c;
    const uint8_t (*g)[FONT_AA_W];
    if (uc >= FONT_AA_FIRST && uc < FONT_AA_FIRST + FONT_AA_COUNT)
        g = font_aa[uc - FONT_AA_FIRST]; // глиф -> указатель на 1-ю строку
    else
        g = font_aa['?' - FONT_AA_FIRST];
    for (int row = 0; row < FONT_AA_H; row++)
        for (int col = 0; col < FONT_AA_W; col++)
            aa_put(x + col, y + row, fg, g[row][col]);
}

void fb_draw_string_aa(const char *s, uint32_t x, uint32_t y, uint32_t fg)
{
    if (!g_present || !s)
        return;
    uint32_t cx = x;
    while (*s) {
        if (*s == '\n') {
            cx = x;
            y += FONT_AA_H + 2;
        } else {
            aa_glyph(*s, cx, y, fg);
            cx += FONT_AA_W + 1;
        }
        s++;
    }
}

void fb_draw_string_aa_bg(const char *s, uint32_t x, uint32_t y,
                          uint32_t fg, uint32_t bg)
{
    if (!g_present || !s)
        return;
    uint32_t cx = x;
    while (*s) {
        if (*s == '\n') {
            cx = x;
            y += FONT_AA_H + 2;
        } else {
            fb_fill_rect(cx, y, FONT_AA_W, FONT_AA_H, bg);
            aa_glyph(*s, cx, y, fg);
            cx += FONT_AA_W + 1;
        }
        s++;
    }
}

void fb_draw_string_aa_bold(const char *s, uint32_t x, uint32_t y, uint32_t fg)
{
    if (!g_present || !s)
        return;
    fb_draw_string_aa(s, x, y, fg);
    fb_draw_string_aa(s, x + 1, y, fg);
}

void fb_flip(void)
{
    if (!g_present)
        return;
    if (g_w > FB_MAX_W || g_h > FB_MAX_H)
        return;
    uint8_t *front = (uint8_t *)(uintptr_t)g_addr;
    uint64_t line_bytes = (uint64_t)g_w * ((g_bpp + 7) / 8);
    // Копируем построчно (pitch фронта может быть шире строки).
    // Кусками по 8 байт через обычные разыменования (x86 терпит невыровненные
    // доступы, libc не нужен — freestanding-safe). ~8x быстрее побайтового.
    for (uint32_t y = 0; y < g_h; y++) {
        uint8_t *dst = front + (uint64_t)y * g_pitch;
        uint8_t *src = g_back + (uint64_t)y * line_bytes;
        uint64_t n64 = line_bytes / 8;
        uint64_t tail = line_bytes % 8;
        uint64_t *d64 = (uint64_t *)(void *)dst;
        uint64_t *s64 = (uint64_t *)(void *)src;
        for (uint64_t i = 0; i < n64; i++)
            d64[i] = s64[i];
        for (uint64_t i = line_bytes - tail; i < line_bytes; i++)
            dst[i] = src[i];
    }
}
