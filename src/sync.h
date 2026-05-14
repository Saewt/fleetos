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
int mutex_unlock(Mutex *m);
void cond_wait(CondVar *cv, Mutex *m, int pid);
int cond_signal(CondVar *cv);
int cond_broadcast(CondVar *cv, int *pids, int max_count);
void sync_set_tick(int tick);
void buffer_init(BoundedBuffer *b);
int buffer_produce(BoundedBuffer *b, int item, int pid, int wake_pids[], int *wake_count);
int buffer_consume(BoundedBuffer *b, int *item, int pid, int wake_pids[], int *wake_count);
void buffer_remove_pid(BoundedBuffer *b, int pid);
void buffer_release_mutex(BoundedBuffer *b, int pid);
void buffer_to_json(BoundedBuffer *b, char *buf, size_t bufsize);

#endif
