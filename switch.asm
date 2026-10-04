; ============================================================================
; switch.asm — Кооперативный переключатель контекста
; ============================================================================
; void task_switch(task_t *old, task_t *new);
;   RDI = old (сохранить RSP по [RDI+0])
;   RSI = new (загрузить RSP из [RSI+0])
; Сохраняем callee-saved регистры System V ABI: RBP, RBX, R12-R15.
; RIP сохраняется неявно через CALL/RET: CALL task_switch кладёт адрес
; возврата, RET в конце прыгает на сохранённый адрес нового потока.
; ============================================================================

[BITS 64]

global task_switch

section .note.GNU-stack
    dd 0, 0, 0, 0

section .text

task_switch:
    ; --- Save callee-saved ------------------------------------------------
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; --- Switch stacks ----------------------------------------------------
    ; task_t.rsp находится по смещению 0, поэтому [RDI] / [RSI].
    mov [rdi], rsp
    mov rsp, [rsi]

    ; --- Restore callee-saved нового потока -------------------------------
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ; Возврат на RIP нового потока (положен task_create как "ret-адрес").
    ret
