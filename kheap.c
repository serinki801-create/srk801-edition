// ============================================================================
// kheap.c — Kernel Heap: статический пул 256 КБ + First-Fit free list
// ============================================================================
// Блок: { size (полезная), free, next }. Выравнивание полезной нагрузки на 8.
// kmalloc: ищет первый free-блок >= size, при необходимости сплитит.
// kfree: помечает free + коалесит соседей.
// Потокобезопасность: вызываем с cli/sti парой (однопроцессорная модель).
// ============================================================================
#include "kheap.h"
#include <stddef.h>
#include <stdint.h>

#define KHEAP_SIZE (256u * 1024u)

typedef struct block {
    size_t size;
    int free;
    struct block *next;
} block_t;

static uint8_t kheap_pool[KHEAP_SIZE] __attribute__((aligned(16)));
static block_t *kheap_head = 0;
static size_t kheap_used_bytes = 0;

static size_t align8(size_t v)
{
    return (v + 7) & ~(size_t)7;
}

void kheap_init(void)
{
    kheap_head = (block_t *)kheap_pool;
    kheap_head->size = KHEAP_SIZE - sizeof(block_t);
    kheap_head->free = 1;
    kheap_head->next = 0;
    kheap_used_bytes = 0;
}

void *kmalloc(size_t size)
{
    if (size == 0)
        return 0;
    size = align8(size);

    __asm__ __volatile__ ("cli");
    block_t *cur = kheap_head;
    while (cur) {
        if (cur->free && cur->size >= size) {
            // Сплит, если остаток вмещает новый заголовок + 8 байт.
            size_t rest = cur->size - size;
            if (rest >= sizeof(block_t) + 8) {
                block_t *nb = (block_t *)((uint8_t *)cur + sizeof(block_t) + size);
                nb->size = rest - sizeof(block_t);
                nb->free = 1;
                nb->next = cur->next;
                cur->size = size;
                cur->next = nb;
            }
            cur->free = 0;
            kheap_used_bytes += cur->size + sizeof(block_t);
            __asm__ __volatile__ ("sti");
            return (void *)((uint8_t *)cur + sizeof(block_t));
        }
        cur = cur->next;
    }
    __asm__ __volatile__ ("sti");
    return 0; // OOM
}

void kfree(void *ptr)
{
    if (!ptr)
        return;
    // Проверка, что указатель внутри пула.
    uint64_t p = (uint64_t)(uintptr_t)ptr;
    uint64_t base = (uint64_t)(uintptr_t)kheap_pool;
    if (p < base + sizeof(block_t) || p >= base + KHEAP_SIZE)
        return;

    __asm__ __volatile__ ("cli");
    block_t *b = (block_t *)(p - sizeof(block_t));
    // Валидация: блок должен быть в цепочке и занят.
    {
        block_t *cur = kheap_head;
        int found = 0;
        while (cur) {
            if (cur == b) { found = 1; break; }
            cur = cur->next;
        }
        if (!found || b->free) {
            __asm__ __volatile__ ("sti");
            return;
        }
    }
    b->free = 1;
    if (kheap_used_bytes >= b->size + sizeof(block_t))
        kheap_used_bytes -= b->size + sizeof(block_t);
    else
        kheap_used_bytes = 0;

    // Коалесинг: склеиваем соседние free-блоки.
    block_t *cur = kheap_head;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            cur->size += sizeof(block_t) + cur->next->size;
            cur->next = cur->next->next;
            continue; // проверяем снова (тройные цепочки)
        }
        cur = cur->next;
    }
    __asm__ __volatile__ ("sti");
}

uint64_t kheap_total(void)      { return KHEAP_SIZE; }
uint64_t kheap_used(void)       { return kheap_used_bytes; }
uint64_t kheap_free_bytes(void)
{
    return KHEAP_SIZE > kheap_used_bytes ? (KHEAP_SIZE - kheap_used_bytes) : 0;
}
