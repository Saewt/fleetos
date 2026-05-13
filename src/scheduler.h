#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "common.h"
#include "pcb.h"

void scheduler_init(void);
void scheduler_add(PCB *pcb);
PCB* scheduler_next(void);
void scheduler_block(PCB *pcb);
void scheduler_unblock(PCB *pcb);
void scheduler_tick(void);
void scheduler_priority_boost(void);

#endif
