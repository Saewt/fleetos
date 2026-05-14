#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "thread.h"

TCB* thread_create(int tid, int owner_pid, CommandType *cmds, int cmd_count, int burst) {
    TCB *tcb = malloc(sizeof(TCB));
    if (!tcb) return NULL;
    tcb->tid = tid;
    tcb->owner_pid = owner_pid;
    tcb->state = T_READY;
    tcb->local_pc = 0;
    tcb->wait_reason = NONE;
    int n = (cmd_count < MAX_COMMANDS) ? cmd_count : MAX_COMMANDS;
    tcb->command_count = n;
    for (int i = 0; i < n; i++) {
        tcb->commands[i] = cmds[i];
    }
    tcb->burst_remaining = burst;
    tcb->command_ticks_remaining = 1;
    return tcb;
}

TCB* thread_spawn(int tid, int owner_pid, CommandType *cmds, int cmd_count, int burst) {
    return thread_create(tid, owner_pid, cmds, cmd_count, burst);
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

CommandType thread_current_command(TCB *tcb) {
    if (!tcb || tcb->command_count <= 0) return CMD_COMPUTE;
    return tcb->commands[tcb->local_pc % tcb->command_count];
}

void thread_execute(TCB *tcb) {
    if (!tcb || tcb->state != T_RUNNING) return;
    if (tcb->burst_remaining <= 0) return;

    tcb->command_ticks_remaining--;
    if (tcb->command_ticks_remaining <= 0) {
        tcb->local_pc = (tcb->local_pc + 1) % tcb->command_count;
        tcb->command_ticks_remaining = 1;
    }
    tcb->burst_remaining--;
}

void thread_to_json(TCB *tcb, char *buf, size_t bufsize) {
    const char *state_str;
    switch (tcb->state) {
        case T_READY:   state_str = "T_READY"; break;
        case T_RUNNING: state_str = "T_RUNNING"; break;
        case T_BLOCKED: state_str = "T_BLOCKED"; break;
        case T_DONE:    state_str = "T_DONE"; break;
        default:        state_str = "UNKNOWN"; break;
    }
    snprintf(buf, bufsize,
        "{\"tid\":%d,\"state\":\"%s\",\"local_pc\":%d,\"wait_reason\":\"%s\",\"burst_remaining\":%d}",
        tcb->tid, state_str, tcb->local_pc, block_reason_to_string(tcb->wait_reason), tcb->burst_remaining);
}
