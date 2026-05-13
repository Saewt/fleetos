#ifndef MEMORY_H
#define MEMORY_H

#include "common.h"
#include "pcb.h"

typedef struct {
    int frames[FRAME_COUNT];
    int last_access[FRAME_COUNT];
    int free_frame_count;
    int current_tick;
} PhysicalMemory;

typedef struct {
    int pid;
    int page;
    int frame;
    int completion_tick;
    int active;
} PendingPageFault;

void mem_init(void);
void mem_set_tick(int tick);
int mem_allocate_page(PCB *pcb, int page);
int mem_access(PCB *pcb, int virtual_addr, int *fault_page);
void mem_free_process(PCB *pcb);
int mem_has_pending_fault(int pid, int *page, int *completion_tick);
void mem_resolve_fault(int pid, int page);
int mem_get_page_count(PCB *pcb);
void mem_to_json(char *buf, size_t bufsize);

#endif
