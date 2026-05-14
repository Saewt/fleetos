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
    CommandType commands[MAX_COMMANDS];
    int command_count;
    int burst_remaining;
    int command_ticks_remaining;
    Priority priority;
    int mlfq_level;
} TCB;

TCB* thread_create(int tid, int owner_pid, CommandType *cmds, int cmd_count, int burst);
TCB* thread_spawn(int tid, int owner_pid, CommandType *cmds, int cmd_count, int burst);
void thread_yield(TCB *tcb);
void thread_block(TCB *tcb, BlockReason reason);
void thread_unblock(TCB *tcb);
void thread_to_json(TCB *tcb, char *buf, size_t bufsize);
CommandType thread_current_command(TCB *tcb);
void thread_execute(TCB *tcb);

#endif
