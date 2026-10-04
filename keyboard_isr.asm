; ============================================================================
; keyboard_isr.asm — Keyboard Interrupt Service Routine (IRQ1)
; ============================================================================
;
; Этот файл содержит ассемблерный обработчик прерывания клавиатуры (IRQ1).
; Он связывается с IDT по вектору 0x21 (после PIC remap).
;
; При прерывании CPU автоматически сохраняет на стек:
;   RIP, CS, RFLAGS, RSP (если ring 0)
;
; Мы дополнительно:
;   1. Сохраняем все GP регистры (RAX-R15).
;   2. Выравниваем стек на 16 байт (для GCC).
;   3. Читаем скан-код из порта 0x60 (INB).
;   4. Передаём скан-код в C-функцию keyboard_handle_scancode(RDI=code).
;   5. Отправляем EOI в PIC (OUTB 0x20, 0x20).
;   6. Восстанавливаем регистры.
;   7. IRETQ — возврат из прерывания.
; ============================================================================

[BITS 64]

global keyboard_irq_handler
extern keyboard_handle_scancode

section .note.GNU-stack
    dd 0, 0, 0, 0
section .text

keyboard_irq_handler:
    ; --- Сохранение всех GP регистров --------------------------------------
    ; Мы сохраняем все 16 GP регистров (RAX-R15), чтобы не испортить
    ; состояние ядра, которое было прервано.
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

    ; --- Выравнивание стека на 16 байт --------------------------------------
    ; При входе в прерывание CPU сохранил 4 значения (RIP, CS, RFLAGS, RSP = 32 B),
    ; затем мы сохранили 15 регистров (120 B). Всего 152 B — НЕ кратно 16.
    ; Согласно System V AMD64 ABI, перед CALL RSP должен быть ≡ 0 (mod 16).
    ; Compensate with 8 bytes (stack alignment for the CALL instruction).
    sub rsp, 8

    ; --- Чтение скан-кода из порта клавиатуры -------------------------------
    ; IN AL, 0x60 — читаем скан-код из порта данных клавиатуры.
    in al, 0x60

    ; Передаём скан-код в RDI (первый аргумент для System V AMD64 ABI).
    mov dil, al

    ; Вызываем C-функцию для обработки скан-кода.
    ; keyboard_handle_scancode(char scancode)
    call keyboard_handle_scancode

    ; Возвращаем выравнивание, добавленное перед CALL.
    add rsp, 8

    ; --- Отправка EOI в PIC -------------------------------------------------
    ; EOI (0x20) записывается в командный порт магистрального PIC.
    ; IRQ1 находится на магистральном PIC (master), порт 0x20.
    mov al, 0x20
    out 0x20, al

    ; --- Восстановка регистров ----------------------------------------------
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

    ; --- Возврат из прерывания ----------------------------------------------
    ; IRETQ — Interrupt Return (Quadword).
    ; Снимает со стека: RIP, CS, RFLAGS, RSP (если был привилегионный переход).
    iretq
