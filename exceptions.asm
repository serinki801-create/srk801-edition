; ============================================================================
; exceptions.asm — CPU Exception stubs (vectors 0-31)
; ============================================================================
; Исключения с кодом ошибки CPU (error code уже в стеке):
;   8 (#DF), 10 (#TS), 11 (#NP), 12 (#SS), 13 (#GP), 14 (#PF),
;   17 (#AC), 21 (#CP), 29 (#VC), 30 (#SX)
; Остальные кода не дают -> кладём заглушку 0.
;
; Каждый стаб приводит стек к единому виду:
;   [RSP]      = vector
;   [RSP+8]    = error code
;   [RSP+16]   = RIP (положен CPU)
;   [RSP+24]   = CS
;   [RSP+32]   = RFLAGS
; Затем прыгаем в exc_common, который сохраняет GP-регистры и зовёт C.
; ============================================================================

[BITS 64]

global exc_common
extern exception_handler

section .note.GNU-stack
    dd 0, 0, 0, 0
section .text

; --- Макросы стабов ----------------------------------------------------------
%macro EXC_NOERR 1
global exc_stub_%1
exc_stub_%1:
    push qword 0        ; dummy error code
    push qword %1       ; vector
    jmp exc_common
%endmacro

%macro EXC_ERR 1
global exc_stub_%1
exc_stub_%1:
    push qword %1       ; vector (error уже положен CPU)
    jmp exc_common
%endmacro

; --- Векторы 0-31 -------------------------------------------------------------
EXC_NOERR 0    ; #DE Divide Error
EXC_NOERR 1    ; #DB Debug
EXC_NOERR 2    ; NMI
EXC_NOERR 3    ; #BP Breakpoint
EXC_NOERR 4    ; #OF Overflow
EXC_NOERR 5    ; #BR Bound Range
EXC_NOERR 6    ; #UD Invalid Opcode
EXC_NOERR 7    ; #NM Device Not Available
EXC_ERR   8    ; #DF Double Fault
EXC_NOERR 9    ; Coprocessor Segment Overrun (legacy)
EXC_ERR   10   ; #TS Invalid TSS
EXC_ERR   11   ; #NP Segment Not Present
EXC_ERR   12   ; #SS Stack-Segment Fault
EXC_ERR   13   ; #GP General Protection Fault
EXC_ERR   14   ; #PF Page Fault
EXC_NOERR 15   ; Reserved
EXC_NOERR 16   ; #MF x87 FPU Error
EXC_ERR   17   ; #AC Alignment Check
EXC_NOERR 18   ; #MC Machine Check
EXC_NOERR 19   ; #XM SIMD Floating-Point
EXC_NOERR 20   ; #VE Virtualization
EXC_ERR   21   ; #CP Control Protection
EXC_NOERR 22   ; Reserved
EXC_NOERR 23   ; Reserved
EXC_NOERR 24   ; Reserved
EXC_NOERR 25   ; Reserved
EXC_NOERR 26   ; Reserved
EXC_NOERR 27   ; Reserved
EXC_NOERR 28   ; #HV Hypervisor Injection
EXC_ERR   29   ; #VC VMM Communication
EXC_ERR   30   ; #SX Security Exception
EXC_NOERR 31   ; Reserved

; --- Общий вход ---------------------------------------------------------------
; На входе: [RSP]=vector, [RSP+8]=error, далее RIP/CS/RFLAGS от CPU.
exc_common:
    ; Сохраняем GP-регистры (порядок важен для exc_regs_t).
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

    ; Выравнивание стека под System V ABI (как в keyboard_isr.asm).
    sub rsp, 8

    ; RDI = указатель на exc_regs_t.
    mov rdi, rsp
    add rdi, 8              ; пропускаем 8 байт выравнивания? нет:
    ; На самом деле regs начинается с RSP+8 из-за sub. Компенсируем:
    ; exc_regs_t лежит по адресу (rsp+8). Передаём именно его.
    call exception_handler

    ; Сюда не должны вернуться (panic внутри), но на всякий случай:
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
    pop rax
    add rsp, 16             ; снять vector + error
    iretq
