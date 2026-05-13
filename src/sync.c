#include <string.h>
#include "sync.h"

void mutex_init(Mutex *m) {
    m->locked = 0;
    m->owner_pid = -1;
    memset(m->wait_queue, -1, sizeof(m->wait_queue));
    m->wait_count = 0;
}

int mutex_lock(Mutex *m, int pid) {
    if (!m->locked) {
        m->locked = 1;
        m->owner_pid = pid;
        return 0;
    }
    /* TODO: Add to wait queue and block */
    (void)pid;
    return -1;
}

void mutex_unlock(Mutex *m) {
    m->locked = 0;
    m->owner_pid = -1;
    /* TODO: Wake up next waiter */
}

void cond_wait(CondVar *cv, Mutex *m) {
    (void)cv;
    (void)m;
    /* TODO: Release mutex and block on condition */
}

void cond_signal(CondVar *cv) {
    (void)cv;
    /* TODO: Wake up one waiter */
}

void cond_broadcast(CondVar *cv) {
    (void)cv;
    /* TODO: Wake up all waiters */
}

void buffer_init(BoundedBuffer *b) {
    memset(b->buffer, 0, sizeof(b->buffer));
    b->in = 0;
    b->out = 0;
    b->count = 0;
    mutex_init(&b->mutex);
    memset(b->not_full.wait_queue, -1, sizeof(b->not_full.wait_queue));
    b->not_full.wait_count = 0;
    memset(b->not_empty.wait_queue, -1, sizeof(b->not_empty.wait_queue));
    b->not_empty.wait_count = 0;
}

int buffer_produce(BoundedBuffer *b, int item) {
    (void)b;
    (void)item;
    /* TODO: Produce item with mutex and condition variables */
    return 0;
}

int buffer_consume(BoundedBuffer *b, int *item) {
    (void)b;
    (void)item;
    /* TODO: Consume item with mutex and condition variables */
    return 0;
}
