; ============================================================================
; mouse_isr.asm — PS/2 Mouse IRQ12 handler (vector 0x2C)
; ============================================================================
; Читает байт из 0x60, зовёт C mouse_tick(AL), шлёт EOI в slave (0xA0)
; и master (0x20). IRQ12 — на slave PIC, нужен EOI обоим.
; ============================================================================

[BITS 64]

global mouse_irq_handler
extern mouse_tick

section .note.GNU-stack
    dd 0, 0, 0, 0
section .text

mouse_irq_handler:
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
    in al, 0x60
    mov dil, al
    call mouse_tick
    add rsp, 8

    ; EOI: сначала slave, потом master.
    mov al, 0x20
    out 0xA0, al
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
