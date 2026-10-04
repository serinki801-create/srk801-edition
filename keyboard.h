// ============================================================================
// keyboard.h — PS/2 Keyboard Driver Header
// ============================================================================

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stddef.h>

// Инициализация драйвера клавиатуры.
void keyboard_init(void);

// Обработка одного скан-кода.
// Возвращает 1, если был нажат Enter (ввод завершён).
int keyboard_handle_scancode(uint8_t scancode);

// Получение текущего буфера ввода (null-terminated строка).
// buffer — выходной буфер.
// max_len — максимальная длина (включая завершающий ноль).
void keyboard_get_input(char *buffer, size_t max_len);

// Проверка готовности ввода (нажат Enter).
int keyboard_is_input_ready(void);

// Очистка состояния ввода после обработки команды.
void keyboard_clear_input(void);

#endif
