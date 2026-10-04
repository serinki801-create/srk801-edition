; ============================================================================
; user.asm — Ring 3 entry/exit, int 0x80 и SYSCALL стабы
; ============================================================================
; switch_to_user_raw(RDI=entry RIP, RSI=user RSP):
;   сохраняет kernel-контекст (RSP/RBP после push rbp), грузит DS/ES/FS/GS
;   пользовательским селектором 0x23, куёт IRETQ-кадр на exec-стеке и уходит
;   в Ring 3 (CS=0x1B, SS=0x23, RFLAGS=0x202).
; user_exit_to_kernel():
;   вызывается из C-обработчика sys_exit (контекст прерывания, IF=0):
;   восстанавливает kernel RSP/RBP и делает RET в точку вызова
;   switch_to_user_raw (в user_run()). После возврата C-код обязан
;   перезагрузить сегменты ядра (SS/DS/ES/FS/GS = 0x10) и TSS.RSP0.
; syscall_int80_handler: вектор 0x80 (DPL=3). RAX=номер, RDI/RSI/RDX=аргументы.
; syscall_entry: точка входа SYSCALL (LSTAR). RCX=RIP, R11=RFLAGS.
;   Оба стаба ведут в C-диспетчер syscall_dispatch(num, a1, a2, a3).
; ============================================================================

[BITS 64]

global switch_to_user_raw
global user_exit_to_kernel
global syscall_int80_handler
global syscall_entry
global syscall_stack_top
global exec_stack_top

extern syscall_dispatch

section .note.GNU-stack
    dd 0, 0, 0, 0

section .bss
align 16
syscall_stack:
    resb 16384
syscall_stack_top:
exec_stack:
    resb 16384
exec_stack_top:

; Сохранённый контекст ядра (для возврата из sys_exit).
saved_kernel_rsp: resq 1
saved_kernel_rbp: resq 1
saved_user_rsp:   resq 1        ; RSP пользователя (для пути SYSCALL)

section .text

; ---------------------------------------------------------------------------
; void switch_to_user_raw(uint64_t entry_rip, uint64_t user_rsp)
; ---------------------------------------------------------------------------
switch_to_user_raw:
    push rbp
    mov [rel saved_kernel_rsp], rsp
    mov [rel saved_kernel_rbp], rbp

    ; Сегменты пользователя (DPL=3, CPL=0 может их грузить).
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; SS трогать нельзя (стек ядра ещё на SS=0x10) — SS=0x23 придёт из кадра.

    ; Куём IRETQ-кадр на отдельном exec-стеке (он будет брошен после IRETQ:
    ; CPU загрузит RSP пользователя из кадра).
    mov rsp, exec_stack_top
    push qword 0x23             ; SS
    push rsi                    ; RSP (пользовательский стек)
    push qword 0x202            ; RFLAGS (bit1 всегда 1 + IF=1)
    push qword 0x1B             ; CS (Ring 3)
    push rdi                    ; RIP (точка входа программы)
    iretq
    ; Сюда не возвращаемся. Возврат идёт через user_exit_to_kernel.
    hlt
    jmp $

; ---------------------------------------------------------------------------
; void user_exit_to_kernel(void) — noreturn в обычном смысле
; ---------------------------------------------------------------------------
user_exit_to_kernel:
    cli
    mov rsp, [rel saved_kernel_rsp]
    pop rbp
    ret                         ; возврат в user_run() (вызывавший switch)

; ---------------------------------------------------------------------------
; syscall_int80_handler — вектор 0x80
; Вход: RAX=номер, RDI/RSI/RDX=аргументы. Возврат: RAX=результат.
; ---------------------------------------------------------------------------
syscall_int80_handler:
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
    ; C(syscall_dispatch): RDI=num, RSI=a1, RDX=a2, RCX=a3.
    ; Исходные регистры затёрты push'ами — достаём сохранённые со стека.
    ; --- Смещения сохранённых регистров (база = rsp+8 после sub): ----------
    ; r15@+0 r14@+8 r13@+16 r12@+24 r11@+32 r10@+40 r9@+48 r8@+56
    ; rbp@+64 rdi@+72 rsi@+80 rdx@+88 rcx@+96 rbx@+104 rax@+112
    mov rdi, [rsp + 8 + 112]    ; num  (saved rax)
    mov rsi, [rsp + 8 + 72]     ; a1   (saved rdi)
    mov rdx, [rsp + 8 + 80]     ; a2   (saved rsi)
    mov rcx, [rsp + 8 + 88]     ; a3   (saved rdx)
    call syscall_dispatch
    ; Результат — в RAX вызванного кода? Нет: dispatcher вернул в RAX,
    ; кладём его в слот saved-rax, чтобы pop вернул его вызывающему.
    mov [rsp + 8 + 112], rax

    add rsp, 8
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
    pop rax                     ; сюда ляжет результат диспетчера
    iretq

; ---------------------------------------------------------------------------
; syscall_entry — точка входа инструкции SYSCALL (MSR LSTAR)
; CPU уже положил: RCX=user RIP, R11=user RFLAGS. RSP — пользовательский!
; ---------------------------------------------------------------------------
syscall_entry:
    mov [rel saved_user_rsp], rsp
    mov rsp, syscall_stack_top
    push rdi                    ; сохранить аргументы пользователя
    push rsi
    push rdx
    push rcx                    ; user RIP
    push r11                    ; user RFLAGS
    ; C ABI: RDI=num RAX->RDI, RSI=a1, RDX=a2, RCX=a3
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax
    call syscall_dispatch
    ; RAX = результат (сохранить для пользователя)
    pop r11
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    mov rsp, [rel saved_user_rsp]
    ; RAX уже содержит возврат — SYSRET его не трогает. RCX/R11 восстановлены.
    db 0x48, 0x0F, 0x07         ; sysretq (REX.W + 0F 07)
