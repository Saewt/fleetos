#ifndef THREAD_H
#define THREAD_H

#include "common.h"

typedef enum {
    T_READY, T_RUNNING, T_BLOCKED, T_DONE
} ThreadState;

typedef struct {
    int tid;
    int owner_pid;
    ThreadState state;
    int local_pc;
    BlockReason wait_reason;
} TCB;

TCB* thread_create(int tid, int owner_pid);
void thread_yield(TCB *tcb);
void thread_block(TCB *tcb, BlockReason reason);
void thread_unblock(TCB *tcb);
const char* thread_to_json(TCB *tcb);

#endif
