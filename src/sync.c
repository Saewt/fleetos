#include <string.h>
#include <stdio.h>
#include "sync.h"
#include "logger.h"

static int sync_tick = 0;

void sync_set_tick(int tick) {
    sync_tick = tick;
}

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
        char data[64];
        snprintf(data, sizeof(data), "{\"pid\":%d,\"owner\":%d}", pid, pid);
        logger_log(sync_tick, "SYNC", LOG_INFO, "Mutex lock acquired", data);
        return 0;
    }
    for (int i = 0; i < m->wait_count; i++) {
        if (m->wait_queue[i] == pid) return -1;
    }
    if (m->wait_count < MAX_PROCS) {
        m->wait_queue[m->wait_count++] = pid;
    }
    char data[64];
    snprintf(data, sizeof(data), "{\"pid\":%d,\"owner\":%d,\"reason\":\"locked\"}", pid, m->owner_pid);
    logger_log(sync_tick, "SYNC", LOG_INFO, "Mutex lock wait", data);
    return -1;
}

int mutex_unlock(Mutex *m) {
    int prev_owner = m->owner_pid;
    int wake_pid = -1;
    if (m->wait_count > 0) {
        wake_pid = m->wait_queue[0];
        for (int i = 1; i < m->wait_count; i++) {
            m->wait_queue[i - 1] = m->wait_queue[i];
        }
        m->wait_count--;
    }
    m->locked = 0;
    m->owner_pid = -1;
    char data[64];
    snprintf(data, sizeof(data), "{\"pid\":%d,\"wake_pid\":%d}", prev_owner, wake_pid);
    logger_log(sync_tick, "SYNC", LOG_INFO, "Mutex unlock", data);
    return wake_pid;
}

void cond_wait(CondVar *cv, Mutex *m, int pid) {
    mutex_unlock(m);
    for (int i = 0; i < cv->wait_count; i++) {
        if (cv->wait_queue[i] == pid) return;
    }
    if (cv->wait_count < MAX_THREADS) {
        cv->wait_queue[cv->wait_count++] = pid;
    }
}

int cond_signal(CondVar *cv) {
    if (cv->wait_count <= 0) return -1;
    int pid = cv->wait_queue[0];
    for (int i = 1; i < cv->wait_count; i++) {
        cv->wait_queue[i - 1] = cv->wait_queue[i];
    }
    cv->wait_count--;
    return pid;
}

int cond_broadcast(CondVar *cv, int *pids, int max_count) {
    int count = 0;
    for (int i = 0; i < cv->wait_count && count < max_count; i++) {
        pids[count++] = cv->wait_queue[i];
    }
    cv->wait_count = 0;
    return count;
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

void buffer_remove_pid(BoundedBuffer *b, int pid) {
    for (int i = 0; i < b->not_full.wait_count; i++) {
        if (b->not_full.wait_queue[i] == pid) {
            b->not_full.wait_queue[i] = -1;
        }
    }
    int w = 0;
    for (int i = 0; i < b->not_full.wait_count; i++) {
        if (b->not_full.wait_queue[i] >= 0) {
            b->not_full.wait_queue[w++] = b->not_full.wait_queue[i];
        }
    }
    b->not_full.wait_count = w;

    for (int i = 0; i < b->not_empty.wait_count; i++) {
        if (b->not_empty.wait_queue[i] == pid) {
            b->not_empty.wait_queue[i] = -1;
        }
    }
    w = 0;
    for (int i = 0; i < b->not_empty.wait_count; i++) {
        if (b->not_empty.wait_queue[i] >= 0) {
            b->not_empty.wait_queue[w++] = b->not_empty.wait_queue[i];
        }
    }
    b->not_empty.wait_count = w;

    for (int i = 0; i < b->mutex.wait_count; i++) {
        if (b->mutex.wait_queue[i] == pid) {
            b->mutex.wait_queue[i] = -1;
        }
    }
    w = 0;
    for (int i = 0; i < b->mutex.wait_count; i++) {
        if (b->mutex.wait_queue[i] >= 0) {
            b->mutex.wait_queue[w++] = b->mutex.wait_queue[i];
        }
    }
    b->mutex.wait_count = w;
}

int buffer_produce(BoundedBuffer *b, int item, int pid, int wake_pids[], int *wake_count) {
    *wake_count = 0;

    if (mutex_lock(&b->mutex, pid) < 0) {
        return -1;
    }

    if (b->count >= RING_BUFFER_SIZE) {
        cond_wait(&b->not_full, &b->mutex, pid);
        return -1;
    }

    b->buffer[b->in] = item;
    b->in = (b->in + 1) % RING_BUFFER_SIZE;
    b->count++;

    int wp = cond_signal(&b->not_empty);
    if (wp >= 0) wake_pids[(*wake_count)++] = wp;

    wp = mutex_unlock(&b->mutex);
    if (wp >= 0 && *wake_count < MAX_WAKE_PIDS) wake_pids[(*wake_count)++] = wp;

    return 0;
}

int buffer_consume(BoundedBuffer *b, int *item, int pid, int wake_pids[], int *wake_count) {
    *wake_count = 0;

    if (mutex_lock(&b->mutex, pid) < 0) {
        return -1;
    }

    if (b->count <= 0) {
        cond_wait(&b->not_empty, &b->mutex, pid);
        return -1;
    }

    *item = b->buffer[b->out];
    b->out = (b->out + 1) % RING_BUFFER_SIZE;
    b->count--;

    int wp = cond_signal(&b->not_full);
    if (wp >= 0) wake_pids[(*wake_count)++] = wp;

    wp = mutex_unlock(&b->mutex);
    if (wp >= 0 && *wake_count < MAX_WAKE_PIDS) wake_pids[(*wake_count)++] = wp;

    return 0;
}

void buffer_release_mutex(BoundedBuffer *b, int pid) {
    if (b->mutex.locked && b->mutex.owner_pid == pid) {
        mutex_unlock(&b->mutex);
    }
}

void buffer_to_json(BoundedBuffer *b, char *buf, size_t bufsize) {
    int pos = 0;
    pos += snprintf(buf + pos, bufsize - pos,
        "{\"count\":%d,\"in\":%d,\"out\":%d,\"mutex_owner\":%d,\"items\":[",
        b->count, b->in, b->out, b->mutex.owner_pid);
    for (int i = 0; i < b->count && i < RING_BUFFER_SIZE; i++) {
        int idx = (b->out + i) % RING_BUFFER_SIZE;
        if (i > 0) pos += snprintf(buf + pos, bufsize - pos, ",");
        pos += snprintf(buf + pos, bufsize - pos, "%d", b->buffer[idx]);
    }
    pos += snprintf(buf + pos, bufsize - pos, "]}");
}
