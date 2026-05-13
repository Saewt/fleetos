#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "scheduler.h"
#include "logger.h"

static PCB *ready_queue[MAX_READY_QUEUE];
static int ready_head = 0;
static int ready_count = 0;
static PCB *current_proc = NULL;
static SchedMode sched_mode = SCHED_RR;
static int quantum_remaining = 0;
static int context_switches = 0;
static int last_tick = 0;

static PCB *proc_table[MAX_PROCS];
static int proc_count = 0;

static int find_in_ready(PCB *pcb) {
    for (int i = 0; i < ready_count; i++) {
        if (ready_queue[(ready_head + i) % MAX_READY_QUEUE] == pcb) {
            return (ready_head + i) % MAX_READY_QUEUE;
        }
    }
    return -1;
}

static void ready_remove_at(int idx) {
    if (ready_count <= 0) return;
    if (idx == ready_head) {
        ready_head = (ready_head + 1) % MAX_READY_QUEUE;
        ready_count--;
        return;
    }
    int tail = (ready_head + ready_count - 1) % MAX_READY_QUEUE;
    int i = idx;
    while (i != tail) {
        int next = (i + 1) % MAX_READY_QUEUE;
        ready_queue[i] = ready_queue[next];
        i = next;
    }
    ready_count--;
}

void scheduler_init(SchedMode mode) {
    ready_head = 0;
    ready_count = 0;
    current_proc = NULL;
    sched_mode = mode;
    quantum_remaining = SCHED_QUANTUM_RR;
    context_switches = 0;
    proc_count = 0;
    memset(ready_queue, 0, sizeof(ready_queue));
    memset(proc_table, 0, sizeof(proc_table));
}

void scheduler_add(PCB *pcb) {
    if (!pcb) return;
    if (find_in_ready(pcb) >= 0) return;
    if (ready_count >= MAX_READY_QUEUE) return;

    int tail = (ready_head + ready_count) % MAX_READY_QUEUE;
    ready_queue[tail] = pcb;
    ready_count++;
    pcb->state = READY;

    int found = 0;
    for (int i = 0; i < proc_count; i++) {
        if (proc_table[i] == pcb) { found = 1; break; }
    }
    if (!found && proc_count < MAX_PROCS) {
        proc_table[proc_count++] = pcb;
    }
}

PCB* scheduler_next(void) {
    if (ready_count <= 0) {
        return current_proc;
    }

    if (current_proc && current_proc->state == RUNNING) {
        pcb_set_state(current_proc, READY);
        int tail = (ready_head + ready_count) % MAX_READY_QUEUE;
        ready_queue[tail] = current_proc;
        ready_count++;
    }

    PCB *next = ready_queue[ready_head];
    ready_head = (ready_head + 1) % MAX_READY_QUEUE;
    ready_count--;

    if (current_proc != next) {
        int from_pid = current_proc ? current_proc->pid : -1;
        int to_pid = next ? next->pid : -1;
        context_switches++;
        char data[64];
        snprintf(data, sizeof(data), "{\"from\":%d,\"to\":%d,\"quantum\":%d}",
                 from_pid, to_pid, quantum_remaining);
        logger_log(last_tick, "SCHED", LOG_INFO, "Context switch", data);
    }

    current_proc = next;
    if (current_proc) {
        pcb_set_state(current_proc, RUNNING);
    }
    quantum_remaining = SCHED_QUANTUM_RR;
    return current_proc;
}

void scheduler_block(PCB *pcb) {
    if (!pcb) return;
    if (pcb->state == TERMINATED) return;

    pcb_set_state(pcb, BLOCKED);

    if (current_proc == pcb) {
        current_proc = NULL;
        scheduler_next();
    } else {
        int idx = find_in_ready(pcb);
        if (idx >= 0) {
            ready_remove_at(idx);
        }
    }
}

void scheduler_unblock(PCB *pcb) {
    if (!pcb) return;
    scheduler_add(pcb);
}

int scheduler_tick(int current_tick) {
    last_tick = current_tick;
    if (!current_proc) {
        scheduler_next();
        return 1;
    }

    quantum_remaining--;
    if (quantum_remaining <= 0) {
        scheduler_next();
        return 1;
    }
    return 0;
}

void scheduler_priority_boost(void) {
    (void)0;
}

PCB* scheduler_get_current(void) {
    return current_proc;
}

PCB** scheduler_get_all_procs(int *count) {
    *count = proc_count;
    return proc_table;
}
