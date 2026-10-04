# ============================================================================
# Makefile — AI-OS x86_64 Build System
# ============================================================================

# Tools.
CC = gcc
# NASM autodetect (Linux Mint / Ubuntu portable):
#  1) system `nasm` from PATH (Mint: sudo apt install nasm) — preferred;
#  2) Flatpak SDK fallback (legacy sandbox path, kept for compatibility).
# NOTE: `AS =` (not `?=`): GNU Make predefines AS=as (GNU assembler),
# so `?=` would silently keep `as` and break Intel-syntax .asm files.
AS = nasm
ifeq (, $(shell command -v nasm 2>/dev/null))
  FLATPAK_NASM := /var/lib/flatpak/runtime/org.freedesktop.Sdk/x86_64/25.08/0cc82216a407cc993941b5ddabd446becc3c9a6219d9bcf50125c60101dcad46/files/bin/nasm
  ifneq (, $(wildcard $(FLATPAK_NASM)))
    AS := $(FLATPAK_NASM)
  endif
endif
LD = ld
GRUB_MKRESCUE = grub-mkrescue
QEMU = qemu-system-x86_64

# Architecture flags.
# -m64              : 64-битный код
# -std=c11            : Си стандарта C11
# -ffreestanding      : freestanding (без стандартной библиотеки)
# -nostdlib           : не линковать libc
# -fno-builtin        : отключить встроенные функции компилятора
# -fno-stack-protector: отключить защиту стека (stack canaries)
# -nostartfiles       : не добавлять CRT (начальные файлы C runtime)
# -mno-red-zone       : отключить red zone (может конфликтовать с прерываниями)
# -fno-pie -fno-pic   : отключить позиционно-независимый код (PIC)
# -mcmodel=large      : большая модель памяти (доступ к любому адресу)
# Безопасность (Phase 5): -fstack-protector-strong вместо -fno-stack-protector.
# Рантайм __stack_chk_guard/__stack_chk_fail — в security.c (ядро) и
# user/user_protect.c (userland). -Wall -Wextra уже включены.
CFLAGS = -m64 -std=c11 -ffreestanding -nostdlib -fno-builtin \
         -fstack-protector-strong -nostartfiles -mno-red-zone \
         -fno-pie -fno-pic -mcmodel=large -Wall -Wextra -g -DKERNEL
ASFLAGS = -f elf64
LDFLAGS = -T linker.ld -nostdlib -static

# Source files (Phase 3: exceptions/panic, PIT, PMM/kheap, tasks;
# Phase 4: VFS/initrd, syscalls/Ring3, ELF, framebuffer).
SRC_C = kernel.c gdt.c idt.c keyboard.c vga.c \
        exceptions.c panic.c timer.c pmm.c kheap.c process.c scheduler.c \
        vfs.c initrd.c syscall.c elf.c framebuffer.c \
        security.c mouse.c wm.c desktop.c minicc.c
SRC_ASM = boot.asm isr.asm gdt_flush.asm keyboard_isr.asm \
          timer_isr.asm switch.asm user.asm mouse_isr.asm
# exceptions.asm собирается отдельно (иначе конфликт с exceptions.c -> exceptions.o).
EXC_ASM_SRC = exceptions.asm
EXC_ASM_OBJ = exceptions_asm.o

# Object files.
OBJ_C = $(SRC_C:.c=.o)
OBJ_ASM = $(SRC_ASM:.asm=.o)
OBJ = $(OBJ_C) $(OBJ_ASM) $(EXC_ASM_OBJ)

# ISO build directories.
ISO_ROOT = iso_root
GRUB_DIR = $(ISO_ROOT)/boot/grub
ISO = ai-os.iso

# --- Phase 4: userland program (Ring 3, ET_EXEC @ 0x400000) -------------------
USER_DIR = user
USER_ELF = user_prog.elf
USER_OBJ = $(USER_DIR)/user_prog.o $(USER_DIR)/user_start.o $(USER_DIR)/user_protect.o
USER_CFLAGS = -m64 -std=c11 -ffreestanding -nostdlib -fno-builtin \
              -fstack-protector-strong -nostartfiles -mno-red-zone \
              -fno-pie -fno-pic -Wall -Wextra -g -I.
USER_LDFLAGS = -T $(USER_DIR)/user.ld -nostdlib -static

# --- Phase 4: initrd (TAR/ustar с user_prog.elf и текстовыми файлами) ---------
INITRD_DIR = initrd_content
INITRD_IMG = initrd.img

.PHONY: all clean run userprog initrd check

all: $(ISO)

# Статические тесты Phase 5 (без QEMU): multiboot, символы, ELF, TAR, bash.
check: kernel.elf $(USER_ELF) $(INITRD_IMG)
	python3 tools_check_phase5.py

# Link ELF kernel.
kernel.elf: $(OBJ) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJ)

# Userland objects and ELF.
$(USER_DIR)/user_prog.o: $(USER_DIR)/user_prog.c syscall.h
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_DIR)/user_protect.o: $(USER_DIR)/user_protect.c syscall.h
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_DIR)/user_start.o: $(USER_DIR)/user_start.asm
	$(AS) $(ASFLAGS) $< -o $@

$(USER_ELF): $(USER_OBJ) $(USER_DIR)/user.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_OBJ)

userprog: $(USER_ELF)

# Initrd image (ustar): packs user_prog.elf + demo text files + lang samples.
$(INITRD_IMG): $(USER_ELF)
	rm -rf $(INITRD_DIR)
	mkdir -p $(INITRD_DIR)
	printf 'Hello from AI-OS initrd (TAR/ustar)!\nThis file was loaded by GRUB as Multiboot2 module.\nTry: cat readme.txt, exec user_prog.elf\n' > $(INITRD_DIR)/hello.txt
	printf 'AI-OS Phase 4 - VFS demo\nCommands: ls, cat <file>, exec <prog>, gui\nFiles: hello.txt, readme.txt, user_prog.elf\n' > $(INITRD_DIR)/readme.txt
	cp $(USER_ELF) $(INITRD_DIR)/
	cp samples/hello.c samples/hello.cpp samples/hello.m samples/hello.swift $(INITRD_DIR)/ 2>/dev/null || true
	tar --format=ustar -cf $@ -C $(INITRD_DIR) .

initrd: $(INITRD_IMG)

# Compile C files.
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Files called from interrupt/exception context must not use SSE
# (XMM-регистры не сохраняются в наших ASM-стабах).
# syscall.o — тоже: диспетчер работает в контексте int 0x80 / SYSCALL.
keyboard.o: CFLAGS += -mno-sse -mno-sse2
vga.o: CFLAGS += -mno-sse -mno-sse2
exceptions.o: CFLAGS += -mno-sse -mno-sse2
panic.o: CFLAGS += -mno-sse -mno-sse2
timer.o: CFLAGS += -mno-sse -mno-sse2
syscall.o: CFLAGS += -mno-sse -mno-sse2
security.o: CFLAGS += -mno-sse -mno-sse2
mouse.o: CFLAGS += -mno-sse -mno-sse2

# Compile assembly files.
%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

# Отдельный объект для стабов исключений (имя отличается от exceptions.o).
$(EXC_ASM_OBJ): $(EXC_ASM_SRC)
	$(AS) $(ASFLAGS) $< -o $@

# Build GRUB configuration directory.
$(GRUB_DIR):
	mkdir -p $@

$(GRUB_DIR)/grub.cfg: grub.cfg | $(GRUB_DIR)
	cp $< $@

# Build ISO image (optional, requires xorriso).
# В ISO кладём ядро и initrd (GRUB подхватит initrd через module2).
$(ISO): kernel.elf $(INITRD_IMG) $(GRUB_DIR)/grub.cfg
	@if command -v xorriso >/dev/null 2>&1; then \
		mkdir -p $(ISO_ROOT)/boot && \
		cp kernel.elf $(ISO_ROOT)/boot/ && \
		cp $(INITRD_IMG) $(ISO_ROOT)/boot/ && \
		$(GRUB_MKRESCUE) -o $@ $(ISO_ROOT); \
	else \
		echo "xorriso not found — skipping ISO build. Use 'make run-kernel' to run QEMU directly."; \
		touch $@; \
	fi

# Clean build artifacts.
clean:
	rm -f $(OBJ) kernel.elf $(USER_OBJ) $(USER_ELF)
	rm -rf $(ISO_ROOT) $(ISO) $(INITRD_DIR) $(INITRD_IMG)

# Run in QEMU (ISO if available, otherwise -kernel).
# -vga std важен для framebuffer (команда gui).
run: $(ISO)
	@if [ -f $(ISO) ] && [ -s $(ISO) ]; then \
		$(QEMU) -cdrom $(ISO) -m 512M -smp 2 -vga std; \
	else \
		$(MAKE) run-kernel; \
	fi

# Run directly with -kernel (no ISO needed).
# Без GRUB-модулей: VFS работает на builtin-файлах, framebuffer отсутствует.
run-kernel: kernel.elf
	$(QEMU) -kernel kernel.elf -m 512M -smp 2 -vga std

# Run with kernel + initrd as multiboot module (без ISO, если QEMU умеет
# -initrd вместе с -kernel; иначе используйте ISO).
run-module: kernel.elf $(INITRD_IMG)
	$(QEMU) -kernel kernel.elf -initrd $(INITRD_IMG) -m 512M -smp 2 -vga std
