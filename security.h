// ============================================================================
// security.h — Аудит и защита ядра (Phase 5)
// ============================================================================
// 1. Stack protector: __stack_chk_guard/__stack_chk_fail для
//    -fstack-protector-strong (ядро freestanding, свой рантайм).
// 2. NX/XD: EFER.NXE + сплит 2-МБ страниц (0-6 МБ) в 4-КБ PT:
//    исполняем только [.text], всё остальное (стеки, куча, BSS) — NX.
//    Остальные 2-МБ страницы (6-512 МБ) помечаются NX целиком;
//    elf_load снимает NX постранично через security_set_exec().
// 3. User Pointer Sanitization: проверка указателей из Ring 3 —
//    разрешены только user-область [0x400000, 512MB) и текущий user-стек.
// ============================================================================
#ifndef SECURITY_H
#define SECURITY_H

#include <stdint.h>

// Включить NX-защиту (EFER.NXE + сплит страниц). После pmm/kheap не зависит,
// вызывать рано (до загрузки ELF/модулей в исполнение — данные не трогаем).
void security_init(void);

// Разрешить/запретить исполнение диапазона [addr, addr+len).
// Возвращает 0 при успехе, -1 если диапазон нельзя отобразить.
// Используется ELF-загрузчиком для сегментов userland.
int security_set_exec(uint64_t addr, uint64_t len, int exec);

// Окно текущего пользовательского стека (ставит switch_to_user_mode,
// сбрасывает после выхода). Нужно санитайзеру (sys_read пишет в стек юзера).
void security_set_user_stack(uint64_t lo, uint64_t hi);

// Проверка указателя из Ring 3: 0 — можно трогать, -1 — запрещено (ядро!).
// len ограничена 1 МБ, проверяется переполнение addr+len.
int security_check_user_ptr(uint64_t addr, uint64_t len);

// Границы user-области (для справки шеллу/тестам).
#define USER_AREA_START 0x400000ULL
#define USER_AREA_END   0x20000000ULL

#endif
