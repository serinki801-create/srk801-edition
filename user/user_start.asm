; ============================================================================
; user_start.asm — Точка входа userland-программы (_start -> main -> sys_exit)
; ============================================================================
[BITS 64]

global _start
extern main

section .note.GNU-stack
    dd 0, 0, 0, 0

section .text
_start:
    ; main() по System V ABI (стек уже выровнен загрузчиком: user_rsp % 16
    ; кратен 16, CALL pushing 8 даёт выравнивание 8 на входе — как обычно).
    call main
    ; main вернулся (наш main не возвращается, но для надёжности):
    ; sys_exit(rax)
    mov rdi, rax
    mov eax, 2                  ; SYS_EXIT
    int 0x80
.hang:
    hlt
    jmp .hang
