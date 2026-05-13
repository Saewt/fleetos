#include <stdlib.h>
#include <string.h>
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
    (void)tcb;
    static char buf[256];
    /* TODO: Implement full JSON serialization */
    buf[0] = '\0';
    return buf;
}
