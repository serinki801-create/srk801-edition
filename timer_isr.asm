; ============================================================================
; timer.asm — PIT IRQ0 handler (vector 0x20)
; ============================================================================
; Сохраняет GP-регистры, выравнивает стек, зовёт C-функцию timer_tick(),
; отправляет EOI в master PIC (0x20) и возвращается через IRETQ.
; ============================================================================

[BITS 64]

global timer_irq_handler
extern timer_tick

section .note.GNU-stack
    dd 0, 0, 0, 0
section .text

timer_irq_handler:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    sub rsp, 8
    call timer_tick
    add rsp, 8

    ; EOI в master PIC (IRQ0 на master).
    mov al, 0x20
    out 0x20, al

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq
