#include <string.h>
#include "memory.h"

static PhysicalMemory mem;

void mem_init(void) {
    memset(mem.frames, -1, sizeof(mem.frames));
    memset(mem.last_access, 0, sizeof(mem.last_access));
    mem.free_frame_count = 16;
}

int mem_allocate_page(int pid, int page) {
    (void)pid;
    (void)page;
    /* TODO: Find free frame using LRU if needed */
    return -1;
}

int mem_access(int pid, int virtual_addr) {
    (void)pid;
    (void)virtual_addr;
    /* TODO: Translate virtual to physical address */
    return -1;
}

void mem_free_process(int pid) {
    (void)pid;
    /* TODO: Free all frames held by process */
}

void mem_page_fault_handler(int pid, int page) {
    (void)pid;
    (void)page;
    /* TODO: Load page into physical memory */
}
