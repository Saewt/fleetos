#ifndef SYNC_H
#define SYNC_H

#include "common.h"

typedef struct {
    int locked;
    int owner_pid;
    int wait_queue[MAX_PROCS];
    int wait_count;
} Mutex;

typedef struct {
    int wait_queue[MAX_THREADS];
    int wait_count;
} CondVar;

typedef struct {
    int buffer[8];
    int in, out, count;
    Mutex mutex;
    CondVar not_full;
    CondVar not_empty;
} BoundedBuffer;

void mutex_init(Mutex *m);
int mutex_lock(Mutex *m, int pid);
void mutex_unlock(Mutex *m);
void cond_wait(CondVar *cv, Mutex *m);
void cond_signal(CondVar *cv);
void cond_broadcast(CondVar *cv);
void buffer_init(BoundedBuffer *b);
int buffer_produce(BoundedBuffer *b, int item);
int buffer_consume(BoundedBuffer *b, int *item);

#endif
