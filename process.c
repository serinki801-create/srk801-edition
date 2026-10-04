// ============================================================================
// process.c — Создание задач ядра (task_create)
// ============================================================================
#include "task.h"
#include <stdint.h>

// Пулы задач и стеков (в BSS, identity-mapped).
static task_t g_tasks[TASK_MAX];
static uint8_t g_stacks[TASK_MAX][TASK_STACK_SIZE] __attribute__((aligned(16)));
static int g_next_pid = 0;

extern void task_switch(task_t *old, task_t *new);

// Внутренний доступ для планировщика.
task_t *process_table(void) { return g_tasks; }

// Заглушка выхода задачи: задача не должна возвращаться, но если вернулась —
// просто крутимся с yield (нужен task_switch, поэтому резолвим через цикл).
// Реализация живёт в scheduler.c (scheduler_yield), здесь weak-заглушка.
__attribute__((weak)) void scheduler_yield(void)
{
    for (;;)
        __asm__ __volatile__ ("hlt");
}

static void task_exit(void)
{
    // Помечаем себя... точный current резолвится планировщиком, здесь halt.
    for (;;)
        __asm__ __volatile__ ("hlt");
}

static void str_copy(char *dst, const char *src, int n)
{
    int i = 0;
    for (; i + 1 < n && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

// Инициализация таблицы: задача 0 = main (текущий контекст).
void process_init(void)
{
    for (int i = 0; i < TASK_MAX; i++) {
        g_tasks[i].rsp = 0;
        g_tasks[i].pid = -1;
        g_tasks[i].state = TASK_EMPTY;
        g_tasks[i].name[0] = '\0';
        g_tasks[i].stack_bottom = 0;
        g_tasks[i].stack_top = 0;
        g_tasks[i].counter = 0;
    }
    g_tasks[0].pid = g_next_pid++;
    g_tasks[0].state = TASK_RUNNING;
    str_copy(g_tasks[0].name, "main", TASK_NAME_LEN);
    g_tasks[0].stack_bottom = 0; // стек main — boot-стек
    g_tasks[0].stack_top = 0;
}

// Создать задачу с точкой входа entry(). Возвращает pid или -1.
int task_create(void (*entry)(void), const char *name)
{
    if (!entry)
        return -1;
    __asm__ __volatile__ ("cli");
    int slot = -1;
    for (int i = 1; i < TASK_MAX; i++) {
        if (g_tasks[i].state == TASK_EMPTY) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        __asm__ __volatile__ ("sti");
        return -1;
    }

    task_t *t = &g_tasks[slot];
    uint64_t top = (uint64_t)(uintptr_t)&g_stacks[slot][TASK_STACK_SIZE];
    top &= ~15ULL; // 16-байтное выравнивание

    // Строим начальный стек (сверху вниз):
    //   [task_exit][entry][r15][r14][r13][r12][rbx][rbp]  <- RSP
    // После task_switch: pops + ret -> RIP=entry, RSP=&task_exit.
    uint64_t *sp = (uint64_t *)top;
    *(--sp) = (uint64_t)(uintptr_t)task_exit; // адрес возврата, если entry вернётся
    *(--sp) = (uint64_t)(uintptr_t)entry;    // RIP для первого RET
    *(--sp) = 0; // r15
    *(--sp) = 0; // r14
    *(--sp) = 0; // r13
    *(--sp) = 0; // r12
    *(--sp) = 0; // rbx
    *(--sp) = 0; // rbp

    t->rsp = (uint64_t)(uintptr_t)sp;
    t->pid = g_next_pid++;
    t->state = TASK_READY;
    str_copy(t->name, name ? name : "task", TASK_NAME_LEN);
    t->stack_bottom = (uint64_t)(uintptr_t)&g_stacks[slot][0];
    t->stack_top = top;
    t->counter = 0;

    __asm__ __volatile__ ("sti");
    return t->pid;
}
