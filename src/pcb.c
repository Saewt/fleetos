#include <stdlib.h>
#include <string.h>
#include "pcb.h"
#include "logger.h"

PCB* pcb_create(int pid, const char *name, Priority priority) {
    PCB *pcb = malloc(sizeof(PCB));
    if (!pcb) return NULL;
    pcb->pid = pid;
    strncpy(pcb->name, name, 31);
    pcb->name[31] = '\0';
    pcb->state = NEW;
    pcb->priority = priority;
    pcb->program_counter = 0;
    memset(pcb->registers, 0, sizeof(pcb->registers));
    memset(pcb->page_table, -1, sizeof(pcb->page_table));
    pcb->pages_used = 0;
    pcb->arrival_time = 0;
    pcb->burst_remaining = 0;
    pcb->wait_time = 0;
    pcb->blocked_reason = NONE;
    memset(pcb->held_resources, 0, sizeof(pcb->held_resources));
    pcb->requested_resource = -1;
    return pcb;
}

void pcb_destroy(PCB *pcb) {
    free(pcb);
}

void pcb_set_state(PCB *pcb, ProcessState state) {
    pcb->state = state;
}

const char* pcb_to_json(PCB *pcb) {
    (void)pcb;
    static char buf[512];
    /* TODO: Implement full JSON serialization */
    buf[0] = '\0';
    return buf;
}
