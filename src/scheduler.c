#include <stdlib.h>
#include <string.h>
#include "scheduler.h"

void scheduler_init(void) {
    /* TODO: Initialize scheduler queues */
}

void scheduler_add(PCB *pcb) {
    (void)pcb;
    /* TODO: Add PCB to appropriate ready queue */
}

PCB* scheduler_next(void) {
    /* TODO: Select next PCB to run */
    return NULL;
}

void scheduler_block(PCB *pcb) {
    (void)pcb;
    /* TODO: Move PCB to blocked state */
}

void scheduler_unblock(PCB *pcb) {
    (void)pcb;
    /* TODO: Move PCB back to ready queue */
}

void scheduler_tick(void) {
    /* TODO: Update quantum and handle preemption */
}

void scheduler_priority_boost(void) {
    /* TODO: Boost starving processes to top queue */
}
