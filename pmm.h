// ============================================================================
// pmm.h — Bitmap Physical Memory Manager (фреймы 4 КБ)
// ============================================================================
#ifndef PMM_H
#define PMM_H

#include <stdint.h>

#define PMM_BLOCK_SIZE 4096u

// Инициализация по карте памяти Multiboot2 (mb_info может быть NULL ->
// fallback на 64 МБ). Вызывать один раз после timer_init().
void pmm_init(void *mb_info);

// Выделить один 4 КБ фрейм. Возвращает физический адрес или 0 (OOM).
uint64_t pmm_alloc_block(void);

// Освободить фрейм по физическому адресу (выровненному на 4 КБ).
void pmm_free_block(uint64_t phys_addr);

// Зарезервировать диапазон [base, base+len) (initrd-модуль, userland ELF).
void pmm_reserve_range(uint64_t base, uint64_t len);

// Статистика.
uint64_t pmm_get_total_mem(void);  // байт всего RAM (по карте, clamp)
uint64_t pmm_get_free_mem(void);   // байт свободно
uint64_t pmm_get_total_blocks(void);
uint64_t pmm_get_free_blocks(void);

#endif
