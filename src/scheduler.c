#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "scheduler.h"
#include "logger.h"

static PCB *rr_ready_queue[MAX_READY_QUEUE];
static int rr_head = 0;
static int rr_count = 0;

static ReadyQueue mlfq_queues[3];
static int mlfq_quantum[3] = {SCHED_QUANTUM_MLFQ_Q0, SCHED_QUANTUM_MLFQ_Q1, SCHED_QUANTUM_MLFQ_Q2};

static PCB *current_proc = NULL;
static SchedMode sched_mode = SCHED_RR;
static int quantum_remaining = 0;
static int context_switches = 0;
static int current_queue = 0;
static int boost_counter = 0;

static PCB *proc_table[MAX_PROCS];
static int proc_count = 0;

static int find_in_queue(ReadyQueue *q, PCB *pcb) {
    for (int i = 0; i < q->count; i++) {
        if (q->queue[(q->head + i) % MAX_PROCS] == pcb) {
            return (q->head + i) % MAX_PROCS;
        }
    }
    return -1;
}

static void queue_remove_at(ReadyQueue *q, int idx) {
    if (q->count <= 0) return;
    if (idx == q->head) {
        q->head = (q->head + 1) % MAX_PROCS;
        q->count--;
        return;
    }
    int tail = (q->head + q->count - 1) % MAX_PROCS;
    int i = idx;
    while (i != tail) {
        int next = (i + 1) % MAX_PROCS;
        q->queue[i] = q->queue[next];
        i = next;
    }
    q->count--;
}

static void queue_push(ReadyQueue *q, PCB *pcb) {
    if (q->count >= MAX_PROCS) return;
    if (find_in_queue(q, pcb) >= 0) return;
    int tail = (q->head + q->count) % MAX_PROCS;
    q->queue[tail] = pcb;
    q->count++;
}

static PCB* queue_pop(ReadyQueue *q) {
    if (q->count <= 0) return NULL;
    PCB *pcb = q->queue[q->head];
    q->head = (q->head + 1) % MAX_PROCS;
    q->count--;
    return pcb;
}

static int get_mlfq_level(Priority p) {
    switch (p) {
        case CRITICAL: return 0;
        case HIGH:     return 0;
        case NORMAL:   return 1;
        case LOW:      return 2;
    }
    return 2;
}

static int mlfq_has_ready(void) {
    for (int i = 0; i < 3; i++) {
        if (mlfq_queues[i].count > 0) return 1;
    }
    return 0;
}

static int find_in_rr(PCB *pcb) {
    for (int i = 0; i < rr_count; i++) {
        if (rr_ready_queue[(rr_head + i) % MAX_READY_QUEUE] == pcb) {
            return (rr_head + i) % MAX_READY_QUEUE;
        }
    }
    return -1;
}

static void rr_remove_at(int idx) {
    if (rr_count <= 0) return;
    if (idx == rr_head) {
        rr_head = (rr_head + 1) % MAX_READY_QUEUE;
        rr_count--;
        return;
    }
    int tail = (rr_head + rr_count - 1) % MAX_READY_QUEUE;
    int i = idx;
    while (i != tail) {
        int next = (i + 1) % MAX_READY_QUEUE;
        rr_ready_queue[i] = rr_ready_queue[next];
        i = next;
    }
    rr_count--;
}

void scheduler_init(SchedMode mode) {
    rr_head = 0;
    rr_count = 0;
    for (int i = 0; i < 3; i++) {
        mlfq_queues[i].head = 0;
        mlfq_queues[i].count = 0;
        memset(mlfq_queues[i].queue, 0, sizeof(mlfq_queues[i].queue));
    }
    current_proc = NULL;
    sched_mode = mode;
    context_switches = 0;
    proc_count = 0;
    boost_counter = 0;
    current_queue = 0;
    memset(rr_ready_queue, 0, sizeof(rr_ready_queue));
    memset(proc_table, 0, sizeof(proc_table));

    if (sched_mode == SCHED_RR) {
        quantum_remaining = SCHED_QUANTUM_RR;
    } else {
        quantum_remaining = SCHED_QUANTUM_MLFQ_Q0;
    }
}

void scheduler_add(PCB *pcb) {
    if (!pcb) return;
    if (sched_mode == SCHED_RR) {
        if (find_in_rr(pcb) >= 0) return;
        if (rr_count >= MAX_READY_QUEUE) return;
        int tail = (rr_head + rr_count) % MAX_READY_QUEUE;
        rr_ready_queue[tail] = pcb;
        rr_count++;
    } else {
        int level = get_mlfq_level(pcb->priority);
        if (find_in_queue(&mlfq_queues[level], pcb) >= 0) return;
        queue_push(&mlfq_queues[level], pcb);
    }
    pcb->state = READY;

    int found = 0;
    for (int i = 0; i < proc_count; i++) {
        if (proc_table[i] == pcb) { found = 1; break; }
    }
    if (!found && proc_count < MAX_PROCS) {
        proc_table[proc_count++] = pcb;
    }
}

PCB* scheduler_next(int tick) {
    if (sched_mode == SCHED_RR) {
        if (rr_count <= 0) {
            return current_proc;
        }

        if (current_proc && current_proc->state == RUNNING) {
            pcb_set_state(current_proc, READY);
            int tail = (rr_head + rr_count) % MAX_READY_QUEUE;
            rr_ready_queue[tail] = current_proc;
            rr_count++;
        }

        PCB *next = rr_ready_queue[rr_head];
        rr_head = (rr_head + 1) % MAX_READY_QUEUE;
        rr_count--;

        if (current_proc != next) {
            int from_pid = current_proc ? current_proc->pid : -1;
            int to_pid = next ? next->pid : -1;
            context_switches++;
            char data[64];
            snprintf(data, sizeof(data), "{\"from\":%d,\"to\":%d,\"quantum\":%d}",
                     from_pid, to_pid, quantum_remaining);
            logger_log(tick, "SCHED", LOG_INFO, "Context switch", data);
        }

        current_proc = next;
        if (current_proc) {
            pcb_set_state(current_proc, RUNNING);
        }
        quantum_remaining = SCHED_QUANTUM_RR;
        return current_proc;
    }

    /* MLFQ path */
    if (!mlfq_has_ready()) {
        return current_proc;
    }

    if (current_proc && current_proc->state == RUNNING) {
        int level = get_mlfq_level(current_proc->priority);
        queue_push(&mlfq_queues[level], current_proc);
        pcb_set_state(current_proc, READY);
    }

    int chosen = -1;
    for (int i = 0; i < 3; i++) {
        if (mlfq_queues[i].count > 0) {
            chosen = i;
            break;
        }
    }

    if (chosen < 0) return NULL;

    PCB *next = queue_pop(&mlfq_queues[chosen]);

    if (current_proc != next) {
        int from_pid = current_proc ? current_proc->pid : -1;
        int to_pid = next ? next->pid : -1;
        context_switches++;
        char data[64];
        snprintf(data, sizeof(data), "{\"from\":%d,\"to\":%d,\"quantum\":%d,\"queue\":%d}",
                 from_pid, to_pid, quantum_remaining, chosen);
        logger_log(tick, "SCHED", LOG_INFO, "Context switch", data);
    }

    current_proc = next;
    current_queue = chosen;
    if (current_proc) {
        pcb_set_state(current_proc, RUNNING);
    }
    quantum_remaining = mlfq_quantum[chosen];
    return current_proc;
}

void scheduler_block(PCB *pcb, int tick) {
    if (!pcb) return;
    if (pcb->state == TERMINATED) return;

    pcb_set_state(pcb, BLOCKED);

    if (current_proc == pcb) {
        current_proc = NULL;
        scheduler_next(tick);
    } else {
        if (sched_mode == SCHED_RR) {
            int idx = find_in_rr(pcb);
            if (idx >= 0) rr_remove_at(idx);
        } else {
            for (int i = 0; i < 3; i++) {
                int idx = find_in_queue(&mlfq_queues[i], pcb);
                if (idx >= 0) {
                    queue_remove_at(&mlfq_queues[i], idx);
                    break;
                }
            }
        }
    }
}

void scheduler_unblock(PCB *pcb) {
    if (!pcb) return;
    scheduler_add(pcb);
}

int scheduler_tick(int tick) {
    if (!current_proc) {
        scheduler_next(tick);
        return 1;
    }

    quantum_remaining--;
    if (quantum_remaining <= 0) {
        if (sched_mode == SCHED_MLFQ && current_proc->state == RUNNING) {
            int next_level = current_queue + 1;
            if (next_level > 2) next_level = 2;
            queue_push(&mlfq_queues[next_level], current_proc);
            pcb_set_state(current_proc, READY);
        }
        scheduler_next(tick);
        boost_counter++;
        if (sched_mode == SCHED_MLFQ && boost_counter >= 30) {
            scheduler_priority_boost();
        }
        return 1;
    }
    return 0;
}

void scheduler_priority_boost(void) {
    if (sched_mode != SCHED_MLFQ) return;

    char data[64];
    snprintf(data, sizeof(data), "{\"boost_tick\":%d}", boost_counter);
    logger_log(0, "SCHED", LOG_INFO, "Priority boost", data);
    boost_counter = 0;

    for (int i = 1; i < 3; i++) {
        while (mlfq_queues[i].count > 0) {
            PCB *pcb = queue_pop(&mlfq_queues[i]);
            queue_push(&mlfq_queues[0], pcb);
        }
    }
}

PCB* scheduler_get_current(void) {
    return current_proc;
}

PCB** scheduler_get_all_procs(int *count) {
    *count = proc_count;
    return proc_table;
}
