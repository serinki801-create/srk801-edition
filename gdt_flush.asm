; ============================================================================
; gdt_flush.asm — GDT reload stubs
; ============================================================================
;
; После загрузки новой GDT через LGDT (выполненной в C),
; мы должны перезагрузить все сегментные регистры.
;
; В Long Mode (64-bit):
;   - Данные сегменты: DS, ES, FS, GS, SS загружаются из нового GDT.
;   - CS перезагружается через far jump (JMP selector:offset).
;
; Мы также загружаем TSS через LTR.
; ============================================================================

[BITS 64]

global gdt_reload

section .note.GNU-stack
    dd 0, 0, 0, 0

gdt_reload:
    ; Перезагружаем сегментные регистры данных.
    ; 0x10 = селектор нашего 64-битного данных (GDT index 2).
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Far jump для перезагрузки CS.
    ; 0x08 = селектор нашего 64-битного кода (GDT index 1).
    ; После far jump CS указывает на наш кодовый дескриптор.
    ; В 64-bit НЕТ far jump immediate (0xEA invalid) — используем push+retf.
    push 0x08               ; новая CS
    mov rax, flush_cs       ; адрес цели
    push rax
    retfq                   ; pop RIP, pop CS (far return)

flush_cs:
    ret

; Загрузка TSS (Task State Segment) через LTR.
; Селектор 0x28 = наш TSS descriptor (GDT index 5, Phase 4).
global load_tss

load_tss:
    mov ax, 0x28
    ltr ax
    ret
