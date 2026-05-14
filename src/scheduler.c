#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "scheduler.h"
#include "logger.h"

static TCB *rr_ready_queue[MAX_READY_QUEUE];
static int rr_head = 0;
static int rr_count = 0;

static ReadyQueue mlfq_queues[3];
static int mlfq_quantum[3] = {SCHED_QUANTUM_MLFQ_Q0, SCHED_QUANTUM_MLFQ_Q1, SCHED_QUANTUM_MLFQ_Q2};

static TCB *current_thread = NULL;
static SchedMode sched_mode = SCHED_RR;
static int quantum_remaining = 0;
static int context_switches = 0;
static int current_queue = 0;
static int last_boost_tick = 0;
static int priority_boost_count = 0;

static TCB *thread_table[MAX_THREADS];
static int thread_count = 0;

static int find_in_queue(ReadyQueue *q, TCB *tcb) {
    for (int i = 0; i < q->count; i++) {
        if (q->queue[(q->head + i) % MAX_READY_QUEUE] == tcb) {
            return (q->head + i) % MAX_READY_QUEUE;
        }
    }
    return -1;
}

static void queue_remove_at(ReadyQueue *q, int idx) {
    if (q->count <= 0) return;
    if (idx == q->head) {
        q->head = (q->head + 1) % MAX_READY_QUEUE;
        q->count--;
        return;
    }
    int tail = (q->head + q->count - 1) % MAX_READY_QUEUE;
    int i = idx;
    while (i != tail) {
        int next = (i + 1) % MAX_READY_QUEUE;
        q->queue[i] = q->queue[next];
        i = next;
    }
    q->count--;
}

static void queue_push(ReadyQueue *q, TCB *tcb) {
    if (q->count >= MAX_READY_QUEUE) return;
    if (find_in_queue(q, tcb) >= 0) return;
    int tail = (q->head + q->count) % MAX_READY_QUEUE;
    q->queue[tail] = tcb;
    q->count++;
}

static TCB* queue_pop(ReadyQueue *q) {
    if (q->count <= 0) return NULL;
    TCB *tcb = q->queue[q->head];
    q->head = (q->head + 1) % MAX_READY_QUEUE;
    q->count--;
    return tcb;
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

static int find_in_rr(TCB *tcb) {
    for (int i = 0; i < rr_count; i++) {
        if (rr_ready_queue[(rr_head + i) % MAX_READY_QUEUE] == tcb) {
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
    current_thread = NULL;
    sched_mode = mode;
    context_switches = 0;
    thread_count = 0;
    last_boost_tick = 0;
    priority_boost_count = 0;
    current_queue = 0;
    memset(rr_ready_queue, 0, sizeof(rr_ready_queue));
    memset(thread_table, 0, sizeof(thread_table));

    if (sched_mode == SCHED_RR) {
        quantum_remaining = SCHED_QUANTUM_RR;
    } else {
        quantum_remaining = SCHED_QUANTUM_MLFQ_Q0;
    }
}

void scheduler_add(TCB *tcb) {
    if (!tcb) return;
    if (sched_mode == SCHED_RR) {
        if (find_in_rr(tcb) >= 0) return;
        if (rr_count >= MAX_READY_QUEUE) return;
        int tail = (rr_head + rr_count) % MAX_READY_QUEUE;
        rr_ready_queue[tail] = tcb;
        rr_count++;
    } else {
        int level = (tcb->mlfq_level >= 0) ? tcb->mlfq_level : get_mlfq_level(tcb->priority);
        if (find_in_queue(&mlfq_queues[level], tcb) >= 0) return;
        queue_push(&mlfq_queues[level], tcb);
        tcb->mlfq_level = level;
    }
    tcb->state = T_READY;

    int found = 0;
    for (int i = 0; i < thread_count; i++) {
        if (thread_table[i] == tcb) { found = 1; break; }
    }
    if (!found && thread_count < MAX_THREADS) {
        thread_table[thread_count++] = tcb;
    }
}

TCB* scheduler_next(int tick) {
    if (sched_mode == SCHED_RR) {
        if (rr_count <= 0) {
            return current_thread;
        }

        if (current_thread && current_thread->state == T_RUNNING) {
            thread_yield(current_thread);
            int tail = (rr_head + rr_count) % MAX_READY_QUEUE;
            rr_ready_queue[tail] = current_thread;
            rr_count++;
        }

        TCB *next = rr_ready_queue[rr_head];
        rr_head = (rr_head + 1) % MAX_READY_QUEUE;
        rr_count--;

        if (current_thread != next) {
            int from_tid = current_thread ? current_thread->tid : -1;
            int to_tid = next ? next->tid : -1;
            context_switches++;
            char data[64];
            snprintf(data, sizeof(data), "{\"from\":%d,\"to\":%d,\"quantum\":%d}",
                     from_tid, to_tid, quantum_remaining);
            logger_log(tick, "SCHED", LOG_INFO, "Context switch", data);
        }

        current_thread = next;
        if (current_thread) {
            current_thread->state = T_RUNNING;
        }
        quantum_remaining = SCHED_QUANTUM_RR;
        return current_thread;
    }

    /* MLFQ path */
    if (!mlfq_has_ready()) {
        return current_thread;
    }

    if (current_thread && current_thread->state == T_RUNNING) {
        int level = (current_thread->mlfq_level >= 0) ? current_thread->mlfq_level : get_mlfq_level(current_thread->priority);
        queue_push(&mlfq_queues[level], current_thread);
        current_thread->mlfq_level = level;
        thread_yield(current_thread);
    }

    int chosen = -1;
    for (int i = 0; i < 3; i++) {
        if (mlfq_queues[i].count > 0) {
            chosen = i;
            break;
        }
    }

    if (chosen < 0) return NULL;

    TCB *next = queue_pop(&mlfq_queues[chosen]);

    if (current_thread != next) {
        int from_tid = current_thread ? current_thread->tid : -1;
        int to_tid = next ? next->tid : -1;
        context_switches++;
        char data[64];
        snprintf(data, sizeof(data), "{\"from\":%d,\"to\":%d,\"quantum\":%d,\"queue\":%d}",
                 from_tid, to_tid, quantum_remaining, chosen);
        logger_log(tick, "SCHED", LOG_INFO, "Context switch", data);
    }

    current_thread = next;
    current_queue = chosen;
    if (current_thread) {
        current_thread->state = T_RUNNING;
    }
    quantum_remaining = mlfq_quantum[chosen];
    return current_thread;
}

void scheduler_block(TCB *tcb, int tick) {
    if (!tcb) return;
    if (tcb->state == T_DONE) return;

    if (sched_mode == SCHED_MLFQ && current_thread == tcb) {
        tcb->mlfq_level = current_queue;
    }

    if (current_thread == tcb) {
        current_thread = NULL;
        scheduler_next(tick);
    } else {
        if (sched_mode == SCHED_RR) {
            int idx = find_in_rr(tcb);
            if (idx >= 0) rr_remove_at(idx);
        } else {
            for (int i = 0; i < 3; i++) {
                int idx = find_in_queue(&mlfq_queues[i], tcb);
                if (idx >= 0) {
                    queue_remove_at(&mlfq_queues[i], idx);
                    break;
                }
            }
        }
    }
}

void scheduler_unblock(TCB *tcb) {
    if (!tcb) return;
    scheduler_add(tcb);
}

void scheduler_terminate(TCB *tcb, int tick) {
    if (!tcb) return;
    if (current_thread == tcb) {
        current_thread = NULL;
    }
    if (sched_mode == SCHED_RR) {
        int idx = find_in_rr(tcb);
        if (idx >= 0) rr_remove_at(idx);
    } else {
        for (int i = 0; i < 3; i++) {
            int idx = find_in_queue(&mlfq_queues[i], tcb);
            if (idx >= 0) {
                queue_remove_at(&mlfq_queues[i], idx);
                break;
            }
        }
    }
    tcb->state = T_DONE;
    (void)tick;
}

int scheduler_tick(int tick) {
    if (sched_mode == SCHED_MLFQ && last_boost_tick == 0) {
        last_boost_tick = tick;
    }

    int do_boost = (sched_mode == SCHED_MLFQ && tick - last_boost_tick >= 30);

    if (!current_thread) {
        scheduler_next(tick);
        if (do_boost) scheduler_priority_boost(tick);
        return 1;
    }

    quantum_remaining--;
    if (quantum_remaining <= 0) {
        if (sched_mode == SCHED_MLFQ && current_thread->state == T_RUNNING) {
            int next_level = current_queue + 1;
            if (next_level > 2) next_level = 2;
            queue_push(&mlfq_queues[next_level], current_thread);
            current_thread->mlfq_level = next_level;
            thread_yield(current_thread);
        }
        scheduler_next(tick);
        if (do_boost) scheduler_priority_boost(tick);
        return 1;
    }

    if (do_boost) scheduler_priority_boost(tick);
    return 0;
}

void scheduler_priority_boost(int tick) {
    if (sched_mode != SCHED_MLFQ) return;

    char data[64];
    snprintf(data, sizeof(data), "{\"tick\":%d,\"boost_count\":%d}", tick, priority_boost_count + 1);
    logger_log(tick, "SCHED", LOG_INFO, "Priority boost", data);
    last_boost_tick = tick;
    priority_boost_count++;

    for (int i = 1; i < 3; i++) {
        while (mlfq_queues[i].count > 0) {
            TCB *tcb = queue_pop(&mlfq_queues[i]);
            tcb->mlfq_level = 0;
            queue_push(&mlfq_queues[0], tcb);
        }
    }
}

TCB* scheduler_get_current(void) {
    return current_thread;
}

TCB** scheduler_get_all_threads(int *count) {
    *count = thread_count;
    return thread_table;
}

int scheduler_get_context_switches(void) {
    return context_switches;
}

int scheduler_get_priority_boost_count(void) {
    return priority_boost_count;
}

void scheduler_to_json(char *buf, size_t bufsize) {
    int pos = 0;
    if (sched_mode == SCHED_RR) {
        pos += snprintf(buf + pos, bufsize - pos,
            "{\"mode\":\"RR\",\"current_tid\":%d,\"quantum_remaining\":%d,\"ready\":[",
            current_thread ? current_thread->tid : -1, quantum_remaining);
        for (int i = 0; i < rr_count; i++) {
            if (i > 0) pos += snprintf(buf + pos, bufsize - pos, ",");
            pos += snprintf(buf + pos, bufsize - pos, "%d",
                rr_ready_queue[(rr_head + i) % MAX_READY_QUEUE]->tid);
        }
        pos += snprintf(buf + pos, bufsize - pos, "]}");
    } else {
        pos += snprintf(buf + pos, bufsize - pos,
            "{\"mode\":\"MLFQ\",\"current_tid\":%d,\"quantum_remaining\":%d,"
            "\"current_queue\":%d,\"priority_boosts\":%d",
            current_thread ? current_thread->tid : -1, quantum_remaining,
            current_queue, priority_boost_count);
        for (int q = 0; q < 3; q++) {
            pos += snprintf(buf + pos, bufsize - pos, ",\"q%d\":[", q);
            for (int i = 0; i < mlfq_queues[q].count; i++) {
                if (i > 0) pos += snprintf(buf + pos, bufsize - pos, ",");
                pos += snprintf(buf + pos, bufsize - pos, "%d",
                    mlfq_queues[q].queue[(mlfq_queues[q].head + i) % MAX_READY_QUEUE]->tid);
            }
            pos += snprintf(buf + pos, bufsize - pos, "]");
        }
        pos += snprintf(buf + pos, bufsize - pos, "}");
    }
}
