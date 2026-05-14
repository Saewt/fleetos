#ifndef KERNEL_H
#define KERNEL_H

#include "common.h"

typedef struct {
    const char *mode;
    int ticks_run;
    int context_switches;
    int priority_boosts;
    int total_wait;
    int max_wait;
    double avg_wait;
    double avg_turnaround;
    int max_turnaround;
    int terminated_count;
} KernelMetrics;

void kernel_init(KernelConfig cfg);
void kernel_run(void);
void kernel_shutdown(void);
KernelMetrics kernel_get_metrics(void);

#endif
