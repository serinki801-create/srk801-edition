// ============================================================================
// mouse.c — PS/2 mouse: init с таймаутами + IRQ12-пакеты
// ============================================================================
// Контроллер 8042: порт 0x64 (команды/статус), 0x60 (данные).
// Включаем AUX-порт (0xA8), разрешаем IRQ12 в compaq-статусе (бит 1 —
// разрешение IRQ12, бит 5 — вкл. мышь), шлём мыши команды с префиксом 0xD4:
// 0xF6 (defaults), 0xF4 (enable reporting). Каждый байт ACK (0xFA) ждём
// с таймаутом ~1мс*100000 итераций — при отсутствии мыши выходим тихо.
// Пакет: [overflow/sign/btn][dx][dy]; бит 3 всегда 1 (рес catch рассинхрона).
// ============================================================================
#include "mouse.h"
#include "idt.h"
#include "port.h"
#include "vga.h"
#include <stdint.h>

#define MOUSE_VECTOR 0x2C
#define MOUSE_MAX_X 1024
#define MOUSE_MAX_Y 768

extern void mouse_irq_handler(void);

static volatile int g_present = 0;
static volatile int g_x = 512, g_y = 384;
static volatile uint8_t g_buttons = 0;
static volatile uint8_t g_cycle = 0;
static volatile int8_t g_packet[3];

static int mouse_wait_write(void)
{
    for (int i = 0; i < 100000; i++) {
        if ((inb(0x64) & 0x02) == 0)
            return 1;
    }
    return 0;
}

static int mouse_wait_read(void)
{
    for (int i = 0; i < 100000; i++) {
        if (inb(0x64) & 0x01)
            return 1;
    }
    return 0;
}

static void mouse_write_cmd(uint8_t cmd)
{
    mouse_wait_write();
    outb(0x64, cmd);
}

static int mouse_write_data(uint8_t data)
{
    // Префикс 0xD4: следующий байт — мыши, не клавиатуре.
    mouse_write_cmd(0xD4);
    if (!mouse_wait_write())
        return 0;
    outb(0x60, data);
    if (!mouse_wait_read())
        return 0;
    return inb(0x60) == 0xFA; // ACK
}

// C-половина IRQ12 (зовётся из NASM-стаба mouse_irq_handler, mouse_isr.asm).
void mouse_tick(uint8_t data)
{
    if (!g_present)
        return;
    switch (g_cycle) {
        case 0:
            // Ресинхронизация: бит 3 обязан быть 1.
            if ((data & 0x08) == 0)
                return;
            g_packet[0] = (int8_t)data;
            g_cycle = 1;
            break;
        case 1:
            g_packet[1] = (int8_t)data;
            g_cycle = 2;
            break;
        case 2: {
            g_packet[2] = (int8_t)data;
            g_cycle = 0;
            g_buttons = (uint8_t)(g_packet[0] & 0x07);
            int dx = g_packet[1];
            int dy = g_packet[2];
            // Знаковые расширения уже в int8_t -> int.
            g_x += dx;
            g_y -= dy; // ось Y мыши инвертирована к экранной
            if (g_x < 0) g_x = 0;
            if (g_y < 0) g_y = 0;
            if (g_x >= MOUSE_MAX_X) g_x = MOUSE_MAX_X - 1;
            if (g_y >= MOUSE_MAX_Y) g_y = MOUSE_MAX_Y - 1;
            break;
        }
    }
}

void mouse_init(void)
{
    __asm__ __volatile__ ("cli");

    // Вектор 0x2C сразу ставим (даже если мыши нет — тихий EOI-обработчик?).
    // Нет: обработчик mouse_irq_handler сам дёргает mouse_tick; без мыши
    // IRQ12 не придёт. Ставим всегда — безопасно.
    idt_set_entry(MOUSE_VECTOR, (uint64_t)mouse_irq_handler);

    // --- Включение AUX-порта ------------------------------------------------
    mouse_write_cmd(0xA8); // enable aux
    // Читаем compaq status, ставим биты 1 (IRQ12) и 5 (aux clock).
    mouse_write_cmd(0x20);
    if (!mouse_wait_read()) {
        __asm__ __volatile__ ("sti");
        vga_print("mouse: no PS/2 controller response\n", 0x07);
        return;
    }
    uint8_t status = inb(0x60);
    status |= 0x02 | 0x20; // IRQ12 + aux enable
    status &= ~(0x10);     // снять бит 4 (не обязательно, оставим как есть)
    mouse_write_cmd(0x60);
    if (!mouse_wait_write()) {
        __asm__ __volatile__ ("sti");
        return;
    }
    outb(0x60, status);

    // --- Команды мыши (каждая обязана вернуть ACK) ---------------------------
    if (!mouse_write_data(0xF6)) { // set defaults
        __asm__ __volatile__ ("sti");
        vga_print("mouse: not detected (no ACK)\n", 0x07);
        return;
    }
    if (!mouse_write_data(0xF4)) { // enable data reporting
        __asm__ __volatile__ ("sti");
        vga_print("mouse: enable failed\n", 0x07);
        return;
    }

    g_present = 1;
    g_x = 512;
    g_y = 384;
    pic_unmask_irq(12); // IRQ12 на slave PIC

    __asm__ __volatile__ ("sti");
    vga_print("mouse: PS/2 detected, IRQ12 open\n", 0x07);
}

int mouse_present(void) { return g_present; }
int mouse_x(void) { return g_x; }
int mouse_y(void) { return g_y; }
uint8_t mouse_buttons(void) { return g_buttons; }
