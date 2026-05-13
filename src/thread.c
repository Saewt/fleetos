#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "thread.h"

TCB* thread_create(int tid, int owner_pid) {
    TCB *tcb = malloc(sizeof(TCB));
    if (!tcb) return NULL;
    tcb->tid = tid;
    tcb->owner_pid = owner_pid;
    tcb->state = T_READY;
    tcb->local_pc = 0;
    tcb->wait_reason = NONE;
    return tcb;
}

void thread_yield(TCB *tcb) {
    if (tcb->state == T_RUNNING) {
        tcb->state = T_READY;
    }
}

void thread_block(TCB *tcb, BlockReason reason) {
    tcb->state = T_BLOCKED;
    tcb->wait_reason = reason;
}

void thread_unblock(TCB *tcb) {
    if (tcb->state == T_BLOCKED) {
        tcb->state = T_READY;
        tcb->wait_reason = NONE;
    }
}

const char* thread_to_json(TCB *tcb) {
    static char buf[256];
    const char *state_str;
    switch (tcb->state) {
        case T_READY:   state_str = "T_READY"; break;
        case T_RUNNING: state_str = "T_RUNNING"; break;
        case T_BLOCKED: state_str = "T_BLOCKED"; break;
        case T_DONE:    state_str = "T_DONE"; break;
        default:        state_str = "UNKNOWN"; break;
    }
    snprintf(buf, sizeof(buf),
        "{\"tid\":%d,\"owner_pid\":%d,\"state\":\"%s\",\"local_pc\":%d}",
        tcb->tid, tcb->owner_pid, state_str, tcb->local_pc);
    return buf;
}
