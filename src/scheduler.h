#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "common.h"
#include "pcb.h"

#define MAX_READY_QUEUE MAX_PROCS

void scheduler_init(SchedMode mode);
void scheduler_add(PCB *pcb);
PCB* scheduler_next(int tick);
void scheduler_block(PCB *pcb, int tick);
void scheduler_unblock(PCB *pcb);
int scheduler_tick(int tick);
void scheduler_priority_boost(void);
PCB* scheduler_get_current(void);
PCB** scheduler_get_all_procs(int *count);

#endif
