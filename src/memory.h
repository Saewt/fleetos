#ifndef MEMORY_H
#define MEMORY_H

#include "common.h"
#include "pcb.h"

typedef struct {
    int frames[FRAME_COUNT];
    int frame_pages[FRAME_COUNT];
    int last_access[FRAME_COUNT];
    int free_frame_count;
    int current_tick;
} PhysicalMemory;

typedef struct {
    int evicted;
    int victim_pid;
    int victim_page;
    int frame;
} EvictionInfo;

void mem_init(void);
void mem_set_tick(int tick);
int mem_allocate_page(PCB *pcb, int page, EvictionInfo *eviction);
int mem_access(PCB *pcb, int virtual_addr, int *fault_page);
void mem_free_process(PCB *pcb);
int mem_get_page_count(PCB *pcb);
void mem_to_json(char *buf, size_t bufsize);

#endif
