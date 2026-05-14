#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "pcb.h"
#include "thread.h"

const char* state_to_string(ProcessState s) {
    switch (s) {
        case NEW:        return "NEW";
        case READY:      return "READY";
        case RUNNING:    return "RUNNING";
        case BLOCKED:    return "BLOCKED";
        case TERMINATED: return "TERMINATED";
        case SUSPENDED:  return "SUSPENDED";
    }
    return "UNKNOWN";
}

const char* priority_to_string(Priority p) {
    switch (p) {
        case CRITICAL: return "CRITICAL";
        case HIGH:     return "HIGH";
        case NORMAL:   return "NORMAL";
        case LOW:      return "LOW";
    }
    return "UNKNOWN";
}

const char* block_reason_to_string(BlockReason r) {
    switch (r) {
        case NONE:           return "NONE";
        case IO_BLOCK:       return "IO_BLOCK";
        case PAGE_FAULT:     return "PAGE_FAULT";
        case MUTEX_WAIT:     return "MUTEX_WAIT";
        case RESOURCE_WAIT:  return "RESOURCE_WAIT";
        case COND_WAIT:      return "COND_WAIT";
    }
    return "UNKNOWN";
}

const char* command_to_string(CommandType c) {
    switch (c) {
        case CMD_COMPUTE:          return "COMPUTE";
        case CMD_PRODUCE_DATA:     return "PRODUCE_DATA";
        case CMD_CONSUME_DATA:     return "CONSUME_DATA";
        case CMD_ACQUIRE_RESOURCE: return "ACQUIRE_RESOURCE";
        case CMD_IO_WRITE:         return "IO_WRITE";
    }
    return "UNKNOWN";
}

const char* sched_mode_to_string(SchedMode m) {
    switch (m) {
        case SCHED_RR:   return "RR";
        case SCHED_MLFQ: return "MLFQ";
    }
    return "UNKNOWN";
}

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
    memset(pcb->held_resources, -1, sizeof(pcb->held_resources));
    pcb->requested_resource = -1;
    memset(pcb->commands, 0, sizeof(pcb->commands));
    pcb->command_count = 0;
    pcb->command_ticks_remaining = 0;
    pcb->mlfq_level = -1;
    pcb->completion_tick = -1;
    return pcb;
}

void pcb_destroy(PCB *pcb) {
    free(pcb);
}

void pcb_set_state(PCB *pcb, ProcessState state) {
    pcb->state = state;
}

void pcb_to_json(PCB *pcb, char *buf, size_t bufsize) {
    snprintf(buf, bufsize,
        "{\"pid\":%d,\"name\":\"%s\",\"state\":\"%s\",\"priority\":\"%s\","
        "\"pc\":%d,\"burst_remaining\":%d,\"wait_time\":%d,"
        "\"blocked_reason\":\"%s\",\"command_ticks_remaining\":%d}",
        pcb->pid, pcb->name,
        state_to_string(pcb->state),
        priority_to_string(pcb->priority),
        pcb->program_counter,
        pcb->burst_remaining,
        pcb->wait_time,
        block_reason_to_string(pcb->blocked_reason),
        pcb->command_ticks_remaining
    );
}

void pcb_to_json_threaded(PCB *pcb, void *tcb, char *buf, size_t bufsize) {
    const char *tcb_json = "";
    char tcb_buf[256];
    if (tcb) {
        thread_to_json((TCB *)tcb, tcb_buf, sizeof(tcb_buf));
        tcb_json = tcb_buf;
    }
    snprintf(buf, bufsize,
        "{\"pid\":%d,\"name\":\"%s\",\"state\":\"%s\",\"priority\":\"%s\","
        "\"pc\":%d,\"burst_remaining\":%d,\"wait_time\":%d,"
        "\"blocked_reason\":\"%s\",\"command_ticks_remaining\":%d,"
        "\"completion_tick\":%d,\"thread\":%s}",
        pcb->pid, pcb->name,
        state_to_string(pcb->state),
        priority_to_string(pcb->priority),
        pcb->program_counter,
        pcb->burst_remaining,
        pcb->wait_time,
        block_reason_to_string(pcb->blocked_reason),
        pcb->command_ticks_remaining,
        pcb->completion_tick,
        tcb_json
    );
}

static TCB* select_primary_thread(TCB **threads, int count, int owner_pid) {
    TCB *running = NULL, *ready = NULL, *blocked = NULL, *done = NULL;
    for (int i = 0; i < count; i++) {
        if (!threads[i] || threads[i]->owner_pid != owner_pid) continue;
        switch (threads[i]->state) {
            case T_RUNNING: if (!running) running = threads[i]; break;
            case T_READY:   if (!ready) ready = threads[i]; break;
            case T_BLOCKED: if (!blocked) blocked = threads[i]; break;
            case T_DONE:    if (!done) done = threads[i]; break;
        }
    }
    if (running) return running;
    if (ready)   return ready;
    if (blocked) return blocked;
    return done;
}

void pcb_to_json_with_threads(PCB *pcb, TCB **threads, int count, char *buf, size_t bufsize) {
    int pos = 0;

    TCB *primary = select_primary_thread(threads, count, pcb->pid);
    char pt_buf[256];
    if (primary) {
        thread_to_json(primary, pt_buf, sizeof(pt_buf));
    } else {
        snprintf(pt_buf, sizeof(pt_buf), "{\"tid\":-1}");
    }

    pos += snprintf(buf + pos, bufsize - pos,
        "{\"pid\":%d,\"name\":\"%s\",\"state\":\"%s\",\"priority\":\"%s\","
        "\"pc\":%d,\"burst_remaining\":%d,\"wait_time\":%d,"
        "\"blocked_reason\":\"%s\",\"command_ticks_remaining\":%d,"
        "\"completion_tick\":%d,"
        "\"thread\":%s,\"threads\":[",
        pcb->pid, pcb->name,
        state_to_string(pcb->state),
        priority_to_string(pcb->priority),
        pcb->program_counter,
        pcb->burst_remaining,
        pcb->wait_time,
        block_reason_to_string(pcb->blocked_reason),
        pcb->command_ticks_remaining,
        pcb->completion_tick,
        pt_buf);

    int added = 0;
    for (int i = 0; i < count; i++) {
        if (!threads[i] || threads[i]->owner_pid != pcb->pid) continue;
        if (added > 0) pos += snprintf(buf + pos, bufsize - pos, ",");
        char t_buf[256];
        thread_to_json(threads[i], t_buf, sizeof(t_buf));
        pos += snprintf(buf + pos, bufsize - pos, "%s", t_buf);
        added++;
    }
    pos += snprintf(buf + pos, bufsize - pos, "]}");
}
