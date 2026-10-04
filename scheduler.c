// ============================================================================
// scheduler.c — Round-Robin поверх task_switch (кооперативный)
// ============================================================================
// main-цикл ядра и демо-задачи периодически зовут scheduler_yield(),
// планировщик отдаёт управление следующей READY/RUNNING задаче по кругу.
// Вытеснения по таймеру нет (таймер только считает тики) — это осознанно:
// preemptive switch внутри IRQ требует отдельного стека и сохранения всего
// контекста, кооперативная модель надёжна в 2-МБ identity-mapping.
// ============================================================================
#include "scheduler.h"
#include "task.h"
#include <stdint.h>

extern void task_switch(task_t *old, task_t *new);
extern void process_init(void);
extern int task_create(void (*entry)(void), const char *name);
extern task_t *process_table(void);

static int g_current = 0;

// Демо-нагрузка: крутим счётчик и отдаём квант.
static void demo_worker(void)
{
    task_t *self = scheduler_current();
    for (;;) {
        if (self)
            self->counter++;
        // Небольшая пауза, чтобы счётчики росли наглядно, но без HLT
        // (HLT внутри задачи ждал бы прерывания — это нормально, но
        //  кооперативный yield показательнее).
        for (volatile int i = 0; i < 200000; i++)
            __asm__ __volatile__ ("" ::: "memory");
        scheduler_yield();
    }
}

static void demo_worker2(void)
{
    task_t *self = scheduler_current();
    for (;;) {
        if (self)
            self->counter += 2;
        for (volatile int i = 0; i < 300000; i++)
            __asm__ __volatile__ ("" ::: "memory");
        scheduler_yield();
    }
}

void scheduler_init(void)
{
    process_init();
    g_current = 0;
    // Две демо-задачи для команды "tasks".
    task_create(demo_worker, "worker1");
    task_create(demo_worker2, "worker2");
}

void scheduler_yield(void)
{
    __asm__ __volatile__ ("cli");
    task_t *table = process_table();

    int prev = g_current;
    int next = -1;
    for (int i = 1; i < TASK_MAX; i++) {
        int idx = (prev + i) % TASK_MAX;
        if (table[idx].state == TASK_READY || table[idx].state == TASK_RUNNING) {
            next = idx;
            break;
        }
    }
    if (next < 0 || next == prev) {
        __asm__ __volatile__ ("sti");
        return; // некому отдавать квант
    }

    if (table[prev].state == TASK_RUNNING)
        table[prev].state = TASK_READY;
    table[next].state = TASK_RUNNING;
    g_current = next;

    // Переключаемся; STI восстановится после возврата (флаг IF сохраняется
    // в RFLAGS, мы чистим cli парой вокруг выбора, сам switch атомарен
    // относительно планировщика, прерывания разрешены внутри задач).
    __asm__ __volatile__ ("sti");
    task_switch(&table[prev], &table[next]);
    // Вернувшись сюда, мы снова задача prev (квант вернулся по кругу).
}

int scheduler_task_count(void)
{
    task_t *table = process_table();
    int n = 0;
    for (int i = 0; i < TASK_MAX; i++)
        if (table[i].state == TASK_READY || table[i].state == TASK_RUNNING)
            n++;
    return n;
}

task_t *scheduler_get_task(int index)
{
    if (index < 0 || index >= TASK_MAX)
        return 0;
    return &process_table()[index];
}

task_t *scheduler_current(void)
{
    return &process_table()[g_current];
}
