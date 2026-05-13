#ifndef PCB_H
#define PCB_H

#include "common.h"

typedef struct {
    int pid;
    char name[32];
    ProcessState state;
    Priority priority;
    int program_counter;
    int registers[8];
    int page_table[16];
    int pages_used;
    int arrival_time;
    int burst_remaining;
    int wait_time;
    BlockReason blocked_reason;
    int held_resources[4];
    int requested_resource;
    CommandType commands[MAX_COMMANDS];
    int command_count;
    int command_ticks_remaining;
} PCB;

PCB* pcb_create(int pid, const char *name, Priority priority);
void pcb_destroy(PCB *pcb);
void pcb_set_state(PCB *pcb, ProcessState state);
const char* pcb_to_json(PCB *pcb);

#endif
