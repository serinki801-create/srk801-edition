// ============================================================================
// kheap.h — Kernel Heap Allocator (First-Fit, kmalloc/kfree)
// ============================================================================
#ifndef KHEAP_H
#define KHEAP_H

#include <stddef.h>
#include <stdint.h>

void kheap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);

// Статистика для meminfo.
uint64_t kheap_total(void);
uint64_t kheap_used(void);
uint64_t kheap_free_bytes(void);

#endif
