// ============================================================================
// scheduler.h — Кооперативный Round-Robin планировщик
// ============================================================================
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "task.h"

void scheduler_init(void);
void scheduler_yield(void);

// Для команды shell "tasks".
int scheduler_task_count(void);
task_t *scheduler_get_task(int index); // 0..TASK_MAX-1
task_t *scheduler_current(void);

#endif
