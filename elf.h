// ============================================================================
// elf.h — ELF64 loader: загрузка исполняемого файла из VFS в память
// ============================================================================
// Поддерживаем ET_EXEC/ET_DYN для x86-64, только PT_LOAD сегменты.
// Память identity-mapped: p_vaddr используется как физический адрес.
// Userland линкуется на базу 0x400000 (см. user/user.ld) — регион обязан
// лежать вне [0, kernel_end) и внутри первых 512 МБ (текущий paging).
// ============================================================================
#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include "vfs.h"

#define ELF_USER_BASE  0x400000ULL
#define ELF_USER_LIMIT 0x20000000ULL // 512 МБ (конец identity-map)

// Загрузить ELF из узла VFS. Возвращает entry point, 0 = ошибка.
// Загруженные PT_LOAD-сегменты резервируются в PMM.
uint64_t elf_load(vfs_node_t *node);

// Проверка ELF-сигнатуры буфера (первые 64 байта достаточно для Ehdr).
int elf_check(const uint8_t *data, uint64_t size);

#endif
