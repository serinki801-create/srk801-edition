// ============================================================================
// task.h — Структура задачи ядра (task_t)
// ============================================================================
#ifndef TASK_H
#define TASK_H

#include <stdint.h>

#define TASK_NAME_LEN 32
#define TASK_STACK_SIZE 8192
#define TASK_MAX 8

typedef enum {
    TASK_EMPTY = 0,
    TASK_READY,
    TASK_RUNNING
} task_state_t;

// ВАЖНО: rsp ДОЛЖЕН быть первым полем (offset 0) — так ждёт switch.asm.
typedef struct task {
    uint64_t rsp;                        // +0: сохранённый стек (switch.asm)
    int pid;                             // ID задачи
    volatile task_state_t state;         // состояние
    char name[TASK_NAME_LEN];            // имя
    uint64_t stack_bottom;               // низ стека (для отладки)
    uint64_t stack_top;                  // верх стека
    volatile uint64_t counter;           // счётчик итераций (демо-нагрузка)
} task_t;

#endif
