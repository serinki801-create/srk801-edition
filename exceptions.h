// ============================================================================
// exceptions.h — CPU Exception handlers (vectors 0-31) + register dump
// ============================================================================
#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#include <stdint.h>

// Каркас регистров, построенный в exceptions.asm (порядок = порядку push).
// rsp указывает на r15.
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;   // номер вектора (0-31), положен стабом
    uint64_t error;    // код ошибки CPU (или 0-заглушка)
    uint64_t rip, cs, rflags;
    // Для ring0->ring0 CPU не кладёт RSP/SS; поле ниже недействительно.
    // Фактический RSP вычисляется как (addr regs)+160.
} __attribute__((packed)) exc_regs_t;

// Устанавливает IDT-записи 0-31 на exc_stub_* (вызывать после idt_init).
void exceptions_init(void);

// C-обработчик, вызываемый из exc_common (exceptions.asm).
// Печатает дамп и уходит в panic(). Не возвращается.
void exception_handler(exc_regs_t *regs);

#endif
