#include <string.h>
#include <stdio.h>
#include "memory.h"
#include "logger.h"

static PhysicalMemory mem;
static PendingPageFault pending_faults[MAX_PROCS];
static int pending_count = 0;

void mem_init(void) {
    memset(mem.frames, -1, sizeof(mem.frames));
    memset(mem.last_access, 0, sizeof(mem.last_access));
    mem.free_frame_count = FRAME_COUNT;
    mem.current_tick = 0;
    pending_count = 0;
    memset(pending_faults, 0, sizeof(pending_faults));
}

void mem_set_tick(int tick) {
    mem.current_tick = tick;
}

static int find_free_frame(void) {
    for (int i = 0; i < FRAME_COUNT; i++) {
        if (mem.frames[i] == -1) return i;
    }
    return -1;
}

static int select_lru_victim(void) {
    int oldest = 0;
    int oldest_tick = mem.last_access[0];
    for (int i = 1; i < FRAME_COUNT; i++) {
        if (mem.last_access[i] < oldest_tick) {
            oldest_tick = mem.last_access[i];
            oldest = i;
        }
    }
    return oldest;
}

int mem_allocate_page(PCB *pcb, int page) {
    if (!pcb || page < 0 || page >= MAX_PAGES) return -1;
    if (pcb->page_table[page] != -1) return pcb->page_table[page];

    int frame = find_free_frame();
    if (frame < 0) {
        frame = select_lru_victim();
        int old_pid = mem.frames[frame];

        char data[128];
        snprintf(data, sizeof(data), "{\"pid\":%d,\"frame\":%d,\"victim_pid\":%d}",
                 pcb->pid, frame, old_pid);
        logger_log(mem.current_tick, "MEM", LOG_WARN, "Page evicted via LRU", data);
    } else {
        mem.free_frame_count--;
    }

    mem.frames[frame] = pcb->pid;
    mem.last_access[frame] = mem.current_tick;

    pcb->page_table[page] = frame;
    pcb->pages_used++;

    char data[128];
    snprintf(data, sizeof(data), "{\"pid\":%d,\"page\":%d,\"frame\":%d,\"free_frames\":%d}",
             pcb->pid, page, frame, mem.free_frame_count);
    logger_log(mem.current_tick, "MEM", LOG_INFO, "Page allocated", data);

    return frame;
}

int mem_access(PCB *pcb, int virtual_addr, int *fault_page) {
    if (!pcb) return -1;

    int page = virtual_addr / PAGE_SIZE;
    int offset = virtual_addr % PAGE_SIZE;

    if (page < 0 || page >= MAX_PAGES) return -1;
    if (pcb->page_table[page] == -1) {
        if (fault_page) *fault_page = page;
        return -1;
    }

    int frame = pcb->page_table[page];
    mem.last_access[frame] = mem.current_tick;

    return frame * PAGE_SIZE + offset;
}

void mem_free_process(PCB *pcb) {
    if (!pcb) return;
    for (int i = 0; i < FRAME_COUNT; i++) {
        if (mem.frames[i] == pcb->pid) {
            mem.frames[i] = -1;
            mem.free_frame_count++;
        }
    }
    memset(pcb->page_table, -1, sizeof(pcb->page_table));
    pcb->pages_used = 0;

    char data[64];
    snprintf(data, sizeof(data), "{\"pid\":%d}", pcb->pid);
    logger_log(mem.current_tick, "MEM", LOG_INFO, "Process memory freed", data);
}

int mem_has_pending_fault(int pid, int *page, int *completion_tick) {
    for (int i = 0; i < pending_count; i++) {
        if (pending_faults[i].active && pending_faults[i].pid == pid) {
            if (mem.current_tick >= pending_faults[i].completion_tick) {
                if (page) *page = pending_faults[i].page;
                if (completion_tick) *completion_tick = pending_faults[i].completion_tick;
                pending_faults[i].active = 0;
                return 1;
            }
        }
    }
    return 0;
}

void mem_resolve_fault(int pid, int page) {
    for (int i = 0; i < pending_count; i++) {
        if (pending_faults[i].active && pending_faults[i].pid == pid &&
            pending_faults[i].page == page) {
            pending_faults[i].active = 0;
            return;
        }
    }
    if (pending_count < MAX_PROCS) {
        pending_faults[pending_count].pid = pid;
        pending_faults[pending_count].page = page;
        pending_faults[pending_count].completion_tick = mem.current_tick + PAGE_FAULT_DURATION;
        pending_faults[pending_count].active = 1;
        pending_count++;
    }
}

int mem_get_page_count(PCB *pcb) {
    if (!pcb) return 0;
    int count = 0;
    for (int i = 0; i < MAX_PAGES; i++) {
        if (pcb->page_table[i] != -1) count++;
    }
    return count;
}

void mem_to_json(char *buf, size_t bufsize) {
    char tmp[64];
    int pos = 0;
    pos += snprintf(buf + pos, bufsize - pos, "\"free_frames\":%d,\"framemap\":[", mem.free_frame_count);
    for (int i = 0; i < FRAME_COUNT; i++) {
        if (i > 0) pos += snprintf(buf + pos, bufsize - pos, ",");
        snprintf(tmp, sizeof(tmp), "{\"frame\":%d,\"pid\":%d}", i, mem.frames[i]);
        pos += snprintf(buf + pos, bufsize - pos, "%s", tmp);
    }
    pos += snprintf(buf + pos, bufsize - pos, "]");
}
