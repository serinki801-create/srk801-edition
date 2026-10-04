// ============================================================================
// process.h — API создания задач
// ============================================================================
#ifndef PROCESS_H
#define PROCESS_H

#include "task.h"

void process_init(void);
int task_create(void (*entry)(void), const char *name);
task_t *process_table(void);

#endif
