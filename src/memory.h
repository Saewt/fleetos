#ifndef MEMORY_H
#define MEMORY_H

#include "common.h"

typedef struct {
    int frames[16];
    int last_access[16];
    int free_frame_count;
} PhysicalMemory;

void mem_init(void);
int mem_allocate_page(int pid, int page);
int mem_access(int pid, int virtual_addr);
void mem_free_process(int pid);
void mem_page_fault_handler(int pid, int page);

#endif
