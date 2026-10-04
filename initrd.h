// ============================================================================
// initrd.h — Initrd на базе TAR (ustar), загружаемый модулем Multiboot2
// ============================================================================
#ifndef INITRD_H
#define INITRD_H

#include <stdint.h>

// Разбор Multiboot2-модулей (tag type 3): первый модуль считается initrd.tar.
// Если модуля нет — создаёт встроенные файлы (hello.txt, readme.txt),
// чтобы команды ls/cat работали и без GRUB-модуля (например, qemu -kernel).
// Возвращает число файлов в корне VFS.
int initrd_init(void *mb_info);

// Количество файлов, загруженных именно из TAR (без builtin).
int initrd_tar_files(void);

// Адрес/размер TAR-модуля (0 если модуля не было). Для резервирования в PMM.
uint64_t initrd_mod_start(void);
uint64_t initrd_mod_end(void);

#endif
