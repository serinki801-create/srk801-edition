// ============================================================================
// pmm.c — Bitmap PMM: парсинг Multiboot2 mmap, аллокация 4 КБ фреймов
// ============================================================================
// Multiboot2 info: u32 total_size, u32 reserved, затем теги {u32 type,u32 size}.
// Тег 6 (mmap): u32 entry_size, u32 entry_version, затем записи:
//   { u64 base, u64 len, u32 type, u32 reserved }, type==1 => доступно.
// Поддерживаем до 1 ГБ физической памяти (262144 фрейма, битмап 32 КБ).
// ============================================================================
#include "pmm.h"
#include <stdint.h>

#define PMM_MAX_PHYS   0x40000000ULL  // 1 ГБ
#define PMM_MAX_FRAMES (PMM_MAX_PHYS / PMM_BLOCK_SIZE) // 262144
#define PMM_BITMAP_SIZE (PMM_MAX_FRAMES / 8)          // 32768 байт

extern char kernel_end[]; // из linker.ld

static uint8_t pmm_bitmap[PMM_BITMAP_SIZE] __attribute__((aligned(4096)));
static uint64_t pmm_total_blocks = 0;
static uint64_t pmm_free_blocks = 0;
static uint64_t pmm_total_mem = 0;

static inline void bmp_set(uint64_t frame)
{
    pmm_bitmap[frame / 8] |= (uint8_t)(1u << (frame % 8));
}

static inline void bmp_clear(uint64_t frame)
{
    pmm_bitmap[frame / 8] &= (uint8_t)~(1u << (frame % 8));
}

static inline int bmp_test(uint64_t frame)
{
    return (pmm_bitmap[frame / 8] >> (frame % 8)) & 1;
}

// Освободить диапазон [base, base+len) пофреймово (только < 1 ГБ).
static void pmm_free_range(uint64_t base, uint64_t len)
{
    uint64_t start = (base + PMM_BLOCK_SIZE - 1) / PMM_BLOCK_SIZE;
    uint64_t end = (base + len) / PMM_BLOCK_SIZE; // конец не включительно
    if (end > PMM_MAX_FRAMES)
        end = PMM_MAX_FRAMES;
    for (uint64_t f = start; f < end; f++) {
        if (bmp_test(f)) {
            bmp_clear(f);
            pmm_free_blocks++;
        }
    }
}

// Занять диапазон [base, base+len).
static void pmm_use_range(uint64_t base, uint64_t len)
{
    uint64_t start = base / PMM_BLOCK_SIZE;
    uint64_t end = (base + len + PMM_BLOCK_SIZE - 1) / PMM_BLOCK_SIZE;
    if (end > PMM_MAX_FRAMES)
        end = PMM_MAX_FRAMES;
    for (uint64_t f = start; f < end; f++) {
        if (!bmp_test(f)) {
            bmp_set(f);
            if (pmm_free_blocks > 0)
                pmm_free_blocks--;
        }
    }
}

void pmm_init(void *mb_info)
{
    // Всё занято по умолчанию.
    for (uint64_t i = 0; i < PMM_BITMAP_SIZE; i++)
        pmm_bitmap[i] = 0xFF;
    pmm_total_blocks = PMM_MAX_FRAMES;
    pmm_free_blocks = 0;
    pmm_total_mem = 0;

    uint64_t kernel_end_phys = (uint64_t)(uintptr_t)kernel_end;

    int found_mmap = 0;

    if (mb_info != 0) {
        uint8_t *ptr = (uint8_t *)mb_info;
        // Базовая валидация указателя (должен быть в первых 512 МБ).
        uint64_t p = (uint64_t)(uintptr_t)ptr;
        if (p >= 0x1000 && p < 0x20000000) {
            uint32_t total_size = *(uint32_t *)(ptr + 0);
            if (total_size >= 8 && total_size < (1u << 20)) {
                uint8_t *tag = ptr + 8;
                uint8_t *end = ptr + total_size;
                while (tag + 8 <= end) {
                    uint32_t type = *(uint32_t *)(tag + 0);
                    uint32_t size = *(uint32_t *)(tag + 4);
                    if (size < 8)
                        break;
                    if (type == 0) // END
                        break;
                    if (type == 6) { // MEMMAP
                        found_mmap = 1;
                        uint32_t entry_size = *(uint32_t *)(tag + 8);
                        // uint32_t entry_ver = *(uint32_t*)(tag+12);
                        uint8_t *ent = tag + 16;
                        uint8_t *tag_end = tag + size;
                        while (ent + entry_size <= tag_end && ent + 24 <= tag_end) {
                            uint64_t base = *(uint64_t *)(ent + 0);
                            uint64_t len  = *(uint64_t *)(ent + 8);
                            uint32_t etype= *(uint32_t *)(ent + 16);
                            if (etype == 1 && len > 0) {
                                if (base < PMM_MAX_PHYS) {
                                    uint64_t capped = base + len;
                                    if (capped > PMM_MAX_PHYS)
                                        len = PMM_MAX_PHYS - base;
                                    pmm_total_mem += len;
                                    pmm_free_range(base, len);
                                }
                            }
                            ent += entry_size;
                        }
                    }
                    uint32_t step = (size + 7) & ~7u;
                    if (step == 0)
                        break;
                    tag += step;
                    if (tag >= end)
                        break;
                }
            }
        }
    }

    if (!found_mmap) {
        // Fallback: считаем, что доступно 64 МБ выше kernel_end.
        pmm_total_mem = 64ULL * 1024 * 1024;
        pmm_free_range(kernel_end_phys, pmm_total_mem - kernel_end_phys);
    }

    // Резервируем всё ниже kernel_end (BIOS, VGA, таблицы, само ядро, стеки).
    // Фреймы [0, kernel_end) -> used.
    {
        uint64_t end_frame = (kernel_end_phys + PMM_BLOCK_SIZE - 1) / PMM_BLOCK_SIZE;
        if (end_frame > PMM_MAX_FRAMES)
            end_frame = PMM_MAX_FRAMES;
        for (uint64_t f = 0; f < end_frame; f++) {
            if (!bmp_test(f)) {
                bmp_set(f);
                if (pmm_free_blocks > 0)
                    pmm_free_blocks--;
            }
        }
    }

    // Явно резервируем нулевой фрейм (NULL-защита), даже если он был free.
    if (!bmp_test(0)) {
        bmp_set(0);
        if (pmm_free_blocks > 0)
            pmm_free_blocks--;
    }
}

// Зарезервировать диапазон [base, base+len) — для initrd-модуля и userland.
// Вызывать после pmm_init (иначе pmm_init перезатрёт битмап).
void pmm_reserve_range(uint64_t base, uint64_t len)
{
    if (len == 0)
        return;
    pmm_use_range(base, len);
}

uint64_t pmm_alloc_block(void)
{
    for (uint64_t f = 1; f < PMM_MAX_FRAMES; f++) {
        if (!bmp_test(f)) {
            bmp_set(f);
            pmm_free_blocks--;
            return f * PMM_BLOCK_SIZE;
        }
    }
    return 0; // OOM
}

void pmm_free_block(uint64_t phys_addr)
{
    if (phys_addr == 0)
        return;
    if (phys_addr & (PMM_BLOCK_SIZE - 1))
        return; // не выровнен
    uint64_t f = phys_addr / PMM_BLOCK_SIZE;
    if (f >= PMM_MAX_FRAMES)
        return;
    if (bmp_test(f)) {
        bmp_clear(f);
        pmm_free_blocks++;
    }
}

uint64_t pmm_get_total_mem(void)   { return pmm_total_mem; }
uint64_t pmm_get_free_mem(void)    { return pmm_free_blocks * PMM_BLOCK_SIZE; }
uint64_t pmm_get_total_blocks(void){ return pmm_total_blocks; }
uint64_t pmm_get_free_blocks(void) { return pmm_free_blocks; }
