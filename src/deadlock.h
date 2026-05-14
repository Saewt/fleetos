#ifndef DEADLOCK_H
#define DEADLOCK_H

#include "common.h"

#define RESOURCE_LANDING_PAD    0
#define RESOURCE_CHARGE_STATION 1
#define RESOURCE_COMM_CHANNEL   2
#define NUM_RESOURCES           3

typedef struct {
    int allocation[MAX_PROCS][MAX_RESOURCES];
    int request[MAX_PROCS][MAX_RESOURCES];
    int available[MAX_RESOURCES];
    int active[MAX_PROCS];
    int wait_pid[MAX_RESOURCES][MAX_PROCS];
    int wait_count[MAX_RESOURCES];
    Priority priority[MAX_PROCS];
} ResourceGraph;

typedef void (*DeadlockVictimCallback)(int pid, int tick);

void deadlock_init(void);
void deadlock_set_tick(int tick);
void deadlock_set_priority(int pid, Priority p);
int deadlock_alloc(int pid, int rid);
/*
 * deadlock_request: attempt to acquire resource.
 * Returns  0 = newly allocated, 1 = already held by this process,
 *         -1 = resource unavailable (process should block).
 */
int deadlock_request(int pid, int rid);
void deadlock_release(int pid, int rid);
int deadlock_detect(int *victim_pid);
void deadlock_resolve(int pid, int tick);
void deadlock_cleanup_process(int pid);
void deadlock_set_victim_callback(DeadlockVictimCallback cb);
int deadlock_get_available(int rid);
void deadlock_wake_waiters(int rid, int tick);
const char* deadlock_resource_name(int rid);

#endif
