// ============================================================================
// elf.c — Парсер ELF64 + загрузчик PT_LOAD в identity-mapped память
// ============================================================================
#include "elf.h"
#include "vfs.h"
#include "vga.h"
#include "kheap.h"
#include "pmm.h"
#include "security.h"
#include <stdint.h>

#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ET_EXEC 2
#define ET_DYN 3
#define EM_X86_64 62
#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PF_R 4

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) Elf64_Phdr;

extern char kernel_end[];

int elf_check(const uint8_t *data, uint64_t size)
{
    if (!data || size < sizeof(Elf64_Ehdr))
        return 0;
    if (data[EI_MAG0] != 0x7F || data[EI_MAG1] != 'E' ||
        data[EI_MAG2] != 'L' || data[EI_MAG3] != 'F')
        return 0;
    if (data[EI_CLASS] != ELFCLASS64 || data[EI_DATA] != ELFDATA2LSB)
        return 0;
    return 1;
}

uint64_t elf_load(vfs_node_t *node)
{
    if (!node || node->type != VFS_FILE || node->size < sizeof(Elf64_Ehdr)) {
        vga_print("elf: bad node\n", 0x0C);
        return 0;
    }

    // Читаем весь файл в буфер ядра (user_prog маленький, ~десятки КБ макс).
    if (node->size > (1u << 20)) {
        vga_print("elf: file too big (>1MB)\n", 0x0C);
        return 0;
    }
    uint8_t *img = (uint8_t *)kmalloc((size_t)node->size);
    if (!img) {
        vga_print("elf: out of heap\n", 0x0C);
        return 0;
    }
    if (vfs_read(node, 0, node->size, img) != node->size) {
        vga_print("elf: read failed\n", 0x0C);
        kfree(img);
        return 0;
    }

    Elf64_Ehdr *eh = (Elf64_Ehdr *)img;
    if (!elf_check(img, node->size)) {
        vga_print("elf: bad magic (not ELF64)\n", 0x0C);
        kfree(img);
        return 0;
    }
    if (eh->e_machine != EM_X86_64) {
        vga_print("elf: not x86-64\n", 0x0C);
        kfree(img);
        return 0;
    }
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) {
        vga_print("elf: not executable/shared\n", 0x0C);
        kfree(img);
        return 0;
    }
    if (eh->e_phnum == 0 || eh->e_phnum > 16 ||
        eh->e_phoff + eh->e_phnum * sizeof(Elf64_Phdr) > node->size) {
        vga_print("elf: bad program headers\n", 0x0C);
        kfree(img);
        return 0;
    }

    uint64_t kernel_end_phys = (uint64_t)(uintptr_t)kernel_end;
    Elf64_Phdr *ph = (Elf64_Phdr *)(img + eh->e_phoff);

    // Предвалидация всех PT_LOAD перед копированием.
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD)
            continue;
        if (ph[i].p_memsz == 0)
            continue;
        uint64_t va = ph[i].p_vaddr;
        if (va < ELF_USER_BASE || va + ph[i].p_memsz > ELF_USER_LIMIT ||
            va + ph[i].p_memsz < va) {
            vga_print("elf: segment outside user area [0x400000, 512MB)\n", 0x0C);
            kfree(img);
            return 0;
        }
        if (va < kernel_end_phys && va + ph[i].p_memsz > 0x100000) {
            vga_print("elf: segment overlaps kernel\n", 0x0C);
            kfree(img);
            return 0;
        }
        if (ph[i].p_offset + ph[i].p_filesz > node->size) {
            vga_print("elf: segment beyond file\n", 0x0C);
            kfree(img);
            return 0;
        }
    }

    // Копирование: filesz -> vaddr, хвост до memsz — нули (BSS).
    // AUDIT (Phase 5): страницы 6-512 МБ по умолчанию NX — снимаем NX
    // с загруженных сегментов, иначе Ring 3 упадёт в #PF на первой
    // инструкции. Ошибка гранта = отказ в загрузке (fail-closed).
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0)
            continue;
        if (security_set_exec(ph[i].p_vaddr, ph[i].p_memsz, 1) != 0) {
            vga_print("elf: NX grant failed (no spare PT?)\n", 0x0C);
            kfree(img);
            return 0;
        }
        uint8_t *dst = (uint8_t *)(uintptr_t)ph[i].p_vaddr;
        for (uint64_t k = 0; k < ph[i].p_filesz; k++)
            dst[k] = img[ph[i].p_offset + k];
        for (uint64_t k = ph[i].p_filesz; k < ph[i].p_memsz; k++)
            dst[k] = 0;
        pmm_reserve_range(ph[i].p_vaddr & ~0xFFFULL,
                          ((ph[i].p_memsz + 0xFFF) & ~0xFFFULL) + 0x1000);
        (void)PF_X; (void)PF_W; (void)PF_R;
    }

    uint64_t entry = eh->e_entry;
    // Для ET_DYN нужна релокация на базу — наш user.ld даёт ET_EXEC, но
    // подстрахуемся: если entry ниже базы, а тип DYN — считаем со смещением.
    if (eh->e_type == ET_DYN && entry < ELF_USER_BASE)
        entry += ELF_USER_BASE;
    if (entry < ELF_USER_BASE || entry >= ELF_USER_LIMIT) {
        vga_print("elf: bad entry point\n", 0x0C);
        kfree(img);
        return 0;
    }

    vga_print("elf: loaded, entry=", 0x0A);
    vga_print_hex64(entry, 0x0A);
    vga_print("\n", 0x0A);
    kfree(img);
    return entry;
}
