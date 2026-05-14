#ifndef PCB_H
#define PCB_H

#include "common.h"
#include "thread.h"

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
    int mlfq_level;
    int completion_tick;
} PCB;

PCB* pcb_create(int pid, const char *name, Priority priority);
void pcb_destroy(PCB *pcb);
void pcb_set_state(PCB *pcb, ProcessState state);
void pcb_to_json(PCB *pcb, char *buf, size_t bufsize);
void pcb_to_json_threaded(PCB *pcb, void *tcb, char *buf, size_t bufsize);
void pcb_to_json_with_threads(PCB *pcb, TCB **threads, int thread_count, char *buf, size_t bufsize);

#endif
