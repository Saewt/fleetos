#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "common.h"
#include "thread.h"

#define MAX_READY_QUEUE MAX_THREADS

typedef struct {
    TCB *queue[MAX_READY_QUEUE];
    int head;
    int count;
} ReadyQueue;

void scheduler_init(SchedMode mode);
void scheduler_add(TCB *tcb);
TCB* scheduler_next(int tick);
void scheduler_block(TCB *tcb, int tick);
void scheduler_unblock(TCB *tcb);
void scheduler_terminate(TCB *tcb, int tick);
int scheduler_tick(int tick);
void scheduler_priority_boost(int tick);
TCB* scheduler_get_current(void);
TCB** scheduler_get_all_threads(int *count);
int scheduler_get_context_switches(void);
int scheduler_get_priority_boost_count(void);
void scheduler_to_json(char *buf, size_t bufsize);

#endif
