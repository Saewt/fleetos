#ifndef DEADLOCK_H
#define DEADLOCK_H

#include "common.h"

typedef struct {
    int allocation[MAX_PROCS][MAX_RESOURCES];
    int request[MAX_PROCS][MAX_RESOURCES];
    int available[MAX_RESOURCES];
} ResourceGraph;

void deadlock_init(void);
void deadlock_alloc(int pid, int rid);
void deadlock_request(int pid, int rid);
void deadlock_release(int pid, int rid);
int deadlock_detect(void);
void deadlock_resolve(void);

#endif
