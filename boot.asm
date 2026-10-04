; ============================================================================
; boot.asm — Multiboot2-compliant x86_64 bootloader stub
; ============================================================================
; GRUB2 loads this file (as an ELF64 or ELF32), parses the Multiboot2 header,
; and jumps to the `start` label in 32-bit protected mode.
;
; Our job here:
;   1. Receive control from GRUB (32-bit protected mode, EBX = MB info pointer).
;   2. Set up a 32-bit stack.
;   3. Enable the A20 line (so we can address memory above 1 MB).
;   4. Transition to 64-bit Long Mode:
;        a) Check CPUID for Long Mode / PAE / MSR support.
;        b) Build identity-mapped page tables (PML4 → PDPT → PDT → 2 MB pages).
;        c) Load CR3 with the physical address of PML4.
;        d) Set CR4.PAE = 1.
;        e) Set IA32_EFER.LME = 1 (MSR 0xC0000080).
;        f) Set CR0.PG = 1 (enable paging).
;   5. Reload segment selectors for 64-bit data segments.
;   6. Far-jump to a `[BITS 64]` label (long_mode_entry).
;   7. Set up a 64-bit stack, pass MB info in RDI (System V AMD64 ABI),
;      and call `kernel_main()` from kernel.c.
;
; Key registers used:
;   EBX — Multiboot info structure pointer (set by GRUB).
;   ESP — 32-bit stack pointer.
;   CR0 — Control register 0 (bit 31 = PG = paging enable).
;   CR3 — Page-map level-4 base (physical address of PML4).
;   CR4 — Control register 4 (bit 5 = PAE).
;   IA32_EFER — Extended Feature Enable Register (MSR 0xC0000080, bit 8 = LME).
;   RDI — First integer argument in System V AMD64 calling convention.
;   RSP — 64-bit stack pointer.
; ============================================================================

; ---------------------------------------------------------------------------
; Multiboot2 Header Constants (строго по спецификации Multiboot2)
; ---------------------------------------------------------------------------
; Заголовок: magic, architecture (=0 для i386), header_length, checksum,
; где magic + architecture + header_length + checksum = 0 (mod 2^32).
; Длина считается ассемблером по меткам — ошибка подсчёта исключена.
; ---------------------------------------------------------------------------
MAGIC       equ  0xE85250D6     ; Multiboot2 magic number

; ---------------------------------------------------------------------------
; Section: Multiboot2 Header
; Must appear in the first 8 KB of the file and be 8-byte aligned.
; Теги заголовка (КАЖДЫЙ тег обязан занимать число байт, кратное 8 —
; GRUB идёт по тегам шагом (size+7)&~7, иначе собьётся и отвергнет заголовок):
;   type=1 (info request, 4 записи): size=24 — meminfo(4)+mmap(6)+fb(8)+имя
;           загрузчика(2)
;   type=5 (framebuffer): size=20 — предпочитаем 1024x768x32
;   type=6 (module alignment): size=8 — выравнивать модули (initrd)
;   type=0 (end): size=8
;   + 4 байта паддинга (входят в header_length).
; Итоговая длина: 16 + 24 + 24(20+паддинг) + 8 + 8 = 80.
; ---------------------------------------------------------------------------
section .multiboot
align 8
mb_header_start:
    dd MAGIC                                ; magic
    dd 0                                    ; architecture = i386
    dd mb_header_end - mb_header_start       ; header_length
    dd -(MAGIC + (mb_header_end - mb_header_start)) ; checksum
    ; --- Information request tag (type 1, footprint 24 = кратно 8) -----------
    dw 1, 0                   ; type=1, flags=0
    dd 24                     ; size = 8 + 4*4
    dd 4                      ; request: basic meminfo
    dd 6                      ; request: memory map (mmap)
    dd 8                      ; request: framebuffer info
    dd 2                      ; request: boot loader name
    ; --- Framebuffer tag (type 5): size=20, footprint 24 (4 паддинга) -------
    ; Обходчик идёт шагом (size+7)&~7 = 24, следующий тег ровно на +24.
    dw 5, 0                   ; type=5, flags=0
    dd 20                     ; size = 20
    dd 1024                   ; width
    dd 768                    ; height
    dd 32                     ; depth (bpp)
    dd 0                      ; паддинг fb-тега до 24 байт footprint
    ; --- Module alignment tag (type 6) -------------------------------------
    dw 6, 0                   ; type=6, flags=0
    dd 8                      ; size = 8
    ; --- End tag (type 0) ---------------------------------------------------
    dw 0, 0                   ; type=0, flags=0
    dd 8                      ; size = 8
mb_header_end:
    ; Итог: 16 + 24 + 24 + 8 + 8 = 80 байт, кратно 8. Контрольная сумма
    ; считается ассемблером по меткам (mb_header_end - mb_header_start).

; ---------------------------------------------------------------------------
; Section: BSS (uninitialized data)
; ---------------------------------------------------------------------------
section .bss
align 16
global stack_bottom
global stack_top

stack_bottom:
    resb 16384                ; Reserve 16 KB for the stack (grows down)
stack_top:

; ---------------------------------------------------------------------------
; Section: Page Tables
; We identity-map the first 2 MB of physical memory so that the kernel can
; access physical addresses (like VGA at 0xB8000) directly as virtual addresses.
;
; Structure (4-level paging, 2 MB "huge" pages):
;   CR3  → PML4  → PDPT  → PDT  → 2 MB physical page
;
; Each entry layout (64-bit):
;   Bits 0-11 : Flags (Present, Writable, PS, etc.)
;   Bits 12-51: Physical page-frame number (PFN)
;   Bits 52-63: Reserved
; ---------------------------------------------------------------------------
section .data
align 0x1000                  ; Page-align each table (4 KB)

; Page-Map Level-4 (PML4): points to the PDPT
pml4:
    dq 0x0000000000000003 + pdpt ; Present(1) + Writable(1) + PFN of PDPT
    times 511 dq 0               ; Remaining 511 entries are empty

; Page-Directory Pointer Table (PDPT): points to the PDT
pdpt:
    dq 0x0000000000000003 + pdt  ; Present + Writable (bit 7 PS НЕ ставим, см. boot.asm выше)
    times 511 dq 0

; Page-Directory Table (PDT): identity-maps first 512 MB (256 x 2 MB pages).
; Заполняется циклом в enable_long_mode; здесь резервируем 512 записей.
pdt:
    times 512 dq 0

; ---------------------------------------------------------------------------
; Section: 32-bit Code
; ---------------------------------------------------------------------------
[BITS 32]
section .text

section .note.GNU-stack
    dd 0, 0, 0, 0

; ---------------------------------------------------------------------------
; Собственный GDT для long_mode_entry: CS=0x08 (64-bit code), DS=CS=0x10.
; Без него boot падает с #GP после enable_long_mode.
; ---------------------------------------------------------------------------
section .data
align 8
gdt_start:
    ; 0x00: Null descriptor
    dq 0
    ; 0x08: 64-bit code segment (L=1), base=0, limit=0xFFFFF (4GB, G=1)
    dd 0x0000FFFF
    dd 0x00AF9A00
    ; 0x10: 64-bit data segment, same limits
    dd 0x0000FFFF
    dd 0x00CF9200
    ; 0x18: 32-bit compat code segment (not used, safety)
    dd 0x0000FFFF
    dd 0x00CF9A00
gdt_end:
gdt32_ptr:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ---------------------------------------------------------------------------
; Секция данных
; ---------------------------------------------------------------------------
section .data
mb_info: dd 0                  ; Storage for Multiboot info pointer
global start                   ; Entry point (called by GRUB)
extern kernel_main             ; Declared in kernel.c

start:
    ; ======================================================================
    ; 32-bit protected-mode entry (set up by GRUB)
    ; ======================================================================
    
    ; ESP = top of stack. Stack grows downward, so we point to the top.
    mov esp, stack_top
    
    ; EBX contains the Multiboot information structure pointer from GRUB.
    ; We save it to memory so we can later pass it to the C kernel.
    mov [mb_info], ebx
    
    ; Disable maskable hardware interrupts (IF = 0 in EFLAGS).
    ; We don't want IRQs firing while we're messing with paging / long mode.
    cli
    
    ; Enable the A20 address line (bit 1 of port 0x92).
    ; Without A20, the CPU wraps addresses at 1 MB (real-mode legacy bug).
    call enable_a20
    
    ; Execute the long-mode transition routine (still 32-bit code).
    call enable_long_mode
    
    ; ======================================================================
    ; At this point: paging is on, CR0.PG=1, CR4.PAE=1, IA32_EFER.LME=1.
    ; However, we're still executing 32-bit code. We need to:
    ;   1. Load 64-bit data-segment selectors (DS/ES/FS/GS/SS).
    ;   2. Far-jump to reload CS with the 64-bit code-segment selector.
    ; ======================================================================
    
    mov ax, 0x10                ; 64-bit data-segment selector (GDT index 2)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Far jump (16-byte descriptor + 32/64-bit offset) to switch CS.
    ; 0x08 = 64-bit code-segment selector (GDT index 1).
    ; long_mode_entry is a `[BITS 64]` label.
    ; NASM 2.16 quirk: `jmp seg:label` in ELF64 emits REX.W prefix which
    ; is wrong here. Manual bytes: EA opcode + offset32 + selector16.
    db 0xEA
    dd long_mode_entry
    dw 0x08

    align 16

; ---------------------------------------------------------------------------
; A20 Line Enable
; ---------------------------------------------------------------------------
enable_a20:
    in al, 0x92                 ; Read byte from port 0x92 (keyboard controller)
    or al, 2                    ; Set bit 1 (Fast A20 gate)
    out 0x92, al                ; Write it back
    ret

; ---------------------------------------------------------------------------
; Long Mode Transition
; ---------------------------------------------------------------------------
enable_long_mode:
    ; --- Check CPUID support -------------------------------------------------
    pushfd                      ; Push EFLAGS onto stack
    pop eax                     ; Read it back into EAX
    mov ecx, eax                ; Save original value in ECX
    xor eax, 1 << 21            ; Flip bit 21 (ID flag)
    push eax
    popfd                       ; Write modified EFLAGS back
    pushfd
    pop eax                     ; Read again
    cmp eax, ecx                ; If unchanged, CPUID is not supported
    je .no_long_mode
    
    ; --- Check for extended CPUID functions (leaf 0x80000001) -----------------
    mov eax, 0x80000000         ; Request highest extended leaf
    cpuid                       ; CPUID instruction: EAX=input, EAX/EBX/ECX/EDX=output
    cmp eax, 0x80000001         ; We need at least leaf 0x80000001
    jb .no_long_mode
    
    ; --- Check Long Mode bit (CPUID.80000001:EDX[29]) ------------------------
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29           ; EDX bit 29 = Long Mode (AMD64 / Intel 64)
    jz .no_long_mode
    
    ; --- Set up page tables (identity map first 512 MB, 256 x 2 MB) ---------
    ; CR3 gets the *physical* address of the PML4 table.
    ; Ядро слинковано на 1 MB (linker.ld: . = 1M), адреса = физические.
    ; Заполняем PDT циклом: entry[i] = (i * 2MB) | Present | Writable | PS.
    mov edi, pdt
    mov ecx, 256                ; 256 записей по 2 MB = 512 MB
    mov eax, 0x83               ; flags: Present(1) + Writable(1) + PS(1<<7)
.fill_pdt:
    mov [edi], eax              ; младшие 32 бита записи
    mov dword [edi + 4], 0      ; старшие 32 бита = 0 (< 4 GB)
    add edi, 8
    add eax, 0x200000           ; следующая 2 MB страница
    loop .fill_pdt

    ; Загружаем свой 32-битный GDT ДО перехода в long mode (CS=0x08, DS=0x10).
    ; GRUB использует свой GDT (0x08 — 32-bit code), но после lgdt
    ; мы можем переиспользовать селекторы 0x08/0x10 внутри своего дескриптора.
    mov eax, gdt32_ptr
    lgdt [eax]

    mov eax, pml4
    mov cr3, eax                ; Load PML4 physical address into CR3
    
    ; --- Enable PAE (Physical Address Extension) ------------------------------
    ; PAE is required for 64-bit paging. CR4 bit 5.
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    
    ; --- Enable Long Mode (IA32_EFER.LME) --------------------------------------
    ; LME = Long Mode Enable, MSR 0xC0000080, bit 8.
    ; This allows the CPU to enter long mode when paging is enabled.
    mov ecx, 0xC0000080         ; ECX = MSR address
    rdmsr                       ; Read IA32_EFER into EDX:EAX
    or eax, 1 << 8
    wrmsr                       ; Write it back
    
    ; --- Enable Paging (CR0.PG) -----------------------------------------------
    ; Paging must be enabled *after* LME is set. CR0 bit 31.
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    
    ; Return to caller (still executing 32-bit-compatible instructions).
    ret

.no_long_mode:
    ; CPU does not support Long Mode — infinite loop.
    hlt
    jmp .no_long_mode

; ---------------------------------------------------------------------------
; 64-bit Long Mode Entry
; ---------------------------------------------------------------------------
[BITS 64]
long_mode_entry:
    ; RSP = top of 64-bit stack
    mov rsp, stack_top
    
    ; Pass Multiboot info pointer to C kernel.
    ; System V AMD64 ABI: RDI = first integer / pointer argument.
    ; mb_info хранит 32-битный физический адрес -> zero-extend в RDI.
    mov edi, [mb_info]
    
    ; Call the C kernel.
    call kernel_main
    
hang:
    hlt
    jmp hang
