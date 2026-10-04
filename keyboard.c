// ============================================================================
// keyboard.c — PS/2 Keyboard Driver
// ============================================================================
//
// Клавиатура подключается к контроллеру прерываний через IRQ1.
// При нажатии/отпускании клавиши генерируется прерывание (IRQ1 -> вектор 0x21).
//
// PS/2 клавиатура использует скан-коды Set 1.
// Make code = код нажатой клавиши.
// Break code = код отпущенной клавиши = make code + 0x80.
//
// Порты ввода-вывода:
//   0x60 — данные клавиатуры (read/write)
//   0x64 — статусный регистр контроллера клавиатуры
//
// Специальные клавиши:
//   Backspace (0x0E) — удаление символа
//   Enter    (0x1C) — завершение ввода
//   Space    (0x39) — пробел
//   Shift    (0x2A, 0x36) — переключение регистра
//   Caps Lock (0x3A) — переключение регистра (только при нажатии, не при отпускании)
// ============================================================================

#include "keyboard.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

// Цвет для VGA вывода (белый на чёрном фоне).
#define COLOR_WHITE 0x07

// Буфер ввода.
#define INPUT_BUFFER_SIZE 256
static char input_buffer[INPUT_BUFFER_SIZE];
static size_t input_len = 0;
static int keyboard_input_ready = 0;

// Состояние модификаторов.
static int shift_pressed = 0;
static int caps_lock = 0;

// Таблица скан-код -> ASCII (нижний регистр, без Shift).
static const char scancode_to_ascii[128] = {
    [0x01] = 0x1B, // Esc
    [0x02] = '1',
    [0x03] = '2',
    [0x04] = '3',
    [0x05] = '4',
    [0x06] = '5',
    [0x07] = '6',
    [0x08] = '7',
    [0x09] = '8',
    [0x0A] = '9',
    [0x0B] = '0',
    [0x0C] = '-',
    [0x0D] = '=',
    [0x0E] = '\b', // Backspace
    [0x0F] = '\t', // Tab
    [0x10] = 'q',
    [0x11] = 'w',
    [0x12] = 'e',
    [0x13] = 'r',
    [0x14] = 't',
    [0x15] = 'y',
    [0x16] = 'u',
    [0x17] = 'i',
    [0x18] = 'o',
    [0x19] = 'p',
    [0x1A] = '[',
    [0x1B] = ']',
    [0x1C] = '\n', // Enter
    [0x1D] = 0x1B, // Left Ctrl (игнорируем)
    [0x1E] = 'a',
    [0x1F] = 's',
    [0x20] = 'd',
    [0x21] = 'f',
    [0x22] = 'g',
    [0x23] = 'h',
    [0x24] = 'j',
    [0x25] = 'k',
    [0x26] = 'l',
    [0x27] = ';',
    [0x28] = '\'',
    [0x29] = '`',
    [0x2A] = 0x00, // Left Shift (make)
    [0x2B] = '\\',
    [0x2C] = 'z',
    [0x2D] = 'x',
    [0x2E] = 'c',
    [0x2F] = 'v',
    [0x30] = 'b',
    [0x31] = 'n',
    [0x32] = 'm',
    [0x33] = ',',
    [0x34] = '.',
    [0x35] = '/',
    [0x36] = 0x00, // Right Shift (make)
    [0x37] = '*',
    [0x38] = 0x1B, // Left Alt (игнорируем)
    [0x39] = ' ',  // Space
};

// Таблица скан-код -> ASCII (с Shift).
static const char scancode_to_ascii_shift[128] = {
    [0x02] = '!',
    [0x03] = '@',
    [0x04] = '#',
    [0x05] = '$',
    [0x06] = '%',
    [0x07] = '^',
    [0x08] = '&',
    [0x09] = '*',
    [0x0A] = '(',
    [0x0B] = ')',
    [0x0C] = '_',
    [0x0D] = '+',
    [0x10] = 'Q',
    [0x11] = 'W',
    [0x12] = 'E',
    [0x13] = 'R',
    [0x14] = 'T',
    [0x15] = 'Y',
    [0x16] = 'U',
    [0x17] = 'I',
    [0x18] = 'O',
    [0x19] = 'P',
    [0x1A] = '{',
    [0x1B] = '}',
    [0x1E] = 'A',
    [0x1F] = 'S',
    [0x20] = 'D',
    [0x21] = 'F',
    [0x22] = 'G',
    [0x23] = 'H',
    [0x24] = 'J',
    [0x25] = 'K',
    [0x26] = 'L',
    [0x27] = ':',
    [0x28] = '"',
    [0x29] = '~',
    [0x2B] = '|',
    [0x2C] = 'Z',
    [0x2D] = 'X',
    [0x2E] = 'C',
    [0x2F] = 'V',
    [0x30] = 'B',
    [0x31] = 'N',
    [0x32] = 'M',
    [0x33] = '<',
    [0x34] = '>',
    [0x35] = '?',
};

// Инициализация клавиатурного драйвера.
void keyboard_init(void)
{
    input_len = 0;
    shift_pressed = 0;
    caps_lock = 0;
    keyboard_input_ready = 0;
}

// Обработка одного скан-кода.
// Возвращает 1, если был нажат Enter (ввод завершён).
int keyboard_handle_scancode(uint8_t scancode)
{
    // Проверяем, является ли это break code (отпускание клавиши).
    int released = (scancode & 0x80) != 0;
    uint8_t make_code = scancode & 0x7F;

    if (released) {
        // Отпускание клавиши.
        if (make_code == 0x2A || make_code == 0x36) {
            // Shift отпущен.
            shift_pressed = 0;
        }
        return 0;
    }

    // Нажатие клавиши (make code).
    switch (make_code) {
        case 0x2A: // Left Shift
        case 0x36: // Right Shift
            shift_pressed = 1;
            return 0;

        case 0x3A: // Caps Lock
            caps_lock = !caps_lock;
            return 0;

        case 0x0E: // Backspace
            if (input_len > 0) {
                input_len--;
                vga_putchar('\b', COLOR_WHITE);
            }
            return 0;

        case 0x1C: // Enter
            vga_putchar('\n', COLOR_WHITE);
            keyboard_input_ready = 1;
            return 1; // Ввод завершён.

        case 0x39: // Space
            if (input_len < INPUT_BUFFER_SIZE - 1) {
                input_buffer[input_len++] = ' ';
                vga_putchar(' ', COLOR_WHITE);
            }
            return 0;

        default: {
            // Обычная клавиша.
            char c = 0;
            if (shift_pressed) {
                c = scancode_to_ascii_shift[make_code];
            }
            if (!c || !shift_pressed) {
                c = scancode_to_ascii[make_code];
            }
            if (c == 0) {
                return 0; // Неизвестная клавиша.
            }

            // Caps Lock: меняем регистр букв.
            if (caps_lock && c >= 'a' && c <= 'z') {
                c = c - 'a' + 'A';
            } else if (caps_lock && c >= 'A' && c <= 'Z') {
                c = c - 'A' + 'a';
            }

            // Добавляем в буфер.
            if (input_len < INPUT_BUFFER_SIZE - 1) {
                input_buffer[input_len++] = c;
                vga_putchar(c, COLOR_WHITE);
            }
            return 0;
        }
    }

    return 0;
}

// Получение текущего буфера ввода (null-terminated).
void keyboard_get_input(char *buffer, size_t max_len)
{
    // AUDIT (Phase 5): защита от max_len==0 (underflow max_len-1) и NULL.
    if (!buffer || max_len == 0)
        return;
    size_t copy_len = input_len < max_len - 1 ? input_len : max_len - 1;
    for (size_t i = 0; i < copy_len; i++) {
        buffer[i] = input_buffer[i];
    }
    buffer[copy_len] = '\0';
    input_len = 0;
}

// Проверка: готов ли ввод (был ли нажат Enter).
int keyboard_is_input_ready(void)
{
    return keyboard_input_ready;
}

// Очистка состояния ввода (после обработки команды).
void keyboard_clear_input(void)
{
    keyboard_input_ready = 0;
    input_len = 0;
}

// ---------------------------------------------------------------------------
// Обработчик прерывания клавиатуры (IRQ1 -> вектор 0x21) находится в
// отдельном ассемблерном файле keyboard_isr.asm.
//
// Причина: в x86_64 ISR нельзя использовать SSE/MMX (XMM-регистры не
// сохраняются автоматически). Поэтому обработчик написан на чистом
// NASM-ассемблере:
//   1. Сохраняет GP-регистры
//   2. Читает скан-код из порта 0x60
//   3. Вызывает C-функцию keyboard_handle_scancode()
//   4. Отправляет EOI в PIC
//   5. Возвращается через IRETQ
// ---------------------------------------------------------------------------
