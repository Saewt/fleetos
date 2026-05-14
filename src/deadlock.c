#include <string.h>
#include <stdio.h>
#include "deadlock.h"
#include "logger.h"

static ResourceGraph graph;
static DeadlockVictimCallback victim_cb = NULL;
static int deadlock_tick = 0;

void deadlock_init(void) {
    memset(graph.allocation, 0, sizeof(graph.allocation));
    memset(graph.request, 0, sizeof(graph.request));
    memset(graph.active, 0, sizeof(graph.active));
    memset(graph.wait_pid, -1, sizeof(graph.wait_pid));
    memset(graph.wait_count, 0, sizeof(graph.wait_count));
    graph.available[RESOURCE_LANDING_PAD] = 1;
    graph.available[RESOURCE_CHARGE_STATION] = 1;
    graph.available[RESOURCE_COMM_CHANNEL] = 1;
}

void deadlock_set_tick(int tick) {
    deadlock_tick = tick;
}

void deadlock_set_priority(int pid, Priority p) {
    if (pid < 0 || pid >= MAX_PROCS) return;
    graph.priority[pid] = p;
}

void deadlock_set_victim_callback(DeadlockVictimCallback cb) {
    victim_cb = cb;
}

int deadlock_get_available(int rid) {
    if (rid < 0 || rid >= NUM_RESOURCES) return -1;
    return graph.available[rid];
}

int deadlock_alloc(int pid, int rid) {
    if (pid < 0 || pid >= MAX_PROCS) return -1;
    if (rid < 0 || rid >= NUM_RESOURCES) return -1;
    if (graph.available[rid] <= 0) return -1;

    graph.available[rid]--;
    graph.allocation[pid][rid]++;
    graph.request[pid][rid] = 0;
    graph.active[pid] = 1;

    char data[128];
    snprintf(data, sizeof(data), "{\"pid\":%d,\"rid\":%d,\"resource\":\"%s\"}",
             pid, rid, deadlock_resource_name(rid));
        logger_log(deadlock_tick, "DEADLOCK", LOG_INFO, "Resource allocated", data);
    return 0;
}

int deadlock_request(int pid, int rid) {
    if (pid < 0 || pid >= MAX_PROCS) return -1;
    if (rid < 0 || rid >= NUM_RESOURCES) return -1;

    if (graph.allocation[pid][rid] > 0) return 1;

    graph.request[pid][rid] = 1;
    graph.active[pid] = 1;

    char data[128];
    snprintf(data, sizeof(data), "{\"pid\":%d,\"rid\":%d,\"resource\":\"%s\",\"available\":%d}",
             pid, rid, deadlock_resource_name(rid), graph.available[rid]);
    logger_log(deadlock_tick, "DEADLOCK", LOG_INFO, "Resource requested", data);

    if (graph.available[rid] > 0) {
        return deadlock_alloc(pid, rid);
    }

    if (graph.wait_count[rid] < MAX_PROCS) {
        graph.wait_pid[rid][graph.wait_count[rid]++] = pid;
    }
    return -1;
}

void deadlock_release(int pid, int rid) {
    if (pid < 0 || pid >= MAX_PROCS) return;
    if (rid < 0 || rid >= NUM_RESOURCES) return;
    if (graph.allocation[pid][rid] <= 0) return;

    graph.allocation[pid][rid]--;
    graph.available[rid]++;

    char data[128];
    snprintf(data, sizeof(data), "{\"pid\":%d,\"rid\":%d,\"resource\":\"%s\"}",
             pid, rid, deadlock_resource_name(rid));
    logger_log(deadlock_tick, "DEADLOCK", LOG_INFO, "Resource released", data);
}

static int is_active(int pid) {
    return pid >= 0 && pid < MAX_PROCS && graph.active[pid];
}

static int has_outgoing_request(int pid) {
    if (!is_active(pid)) return 0;
    for (int r = 0; r < NUM_RESOURCES; r++) {
        if (graph.request[pid][r] > 0) return 1;
    }
    return 0;
}

static int dfs_cycle(int pid, int *visited, int *in_stack, int *counter, int *progress) {
    visited[pid] = 1;
    in_stack[pid] = 1;

    for (int r = 0; r < NUM_RESOURCES; r++) {
        if (graph.request[pid][r] <= 0) continue;

        for (int q = 0; q < MAX_PROCS; q++) {
            if (!is_active(q)) continue;
            if (q == pid) continue;
            if (graph.allocation[q][r] <= 0) continue;

            if (!visited[q]) {
                if (dfs_cycle(q, visited, in_stack, counter, progress)) {
                    return 1;
                }
            } else if (in_stack[q]) {
                return 1;
            }
        }
    }

    in_stack[pid] = 0;
    *progress = 1;
    return 0;
}

int deadlock_detect(int *victim_pid) {
    int visited[MAX_PROCS] = {0};
    int in_stack[MAX_PROCS] = {0};

    int out_victim = -1;

    for (int pid = 0; pid < MAX_PROCS; pid++) {
        if (!is_active(pid)) continue;
        if (!has_outgoing_request(pid)) continue;

        memset(visited, 0, sizeof(visited));
        memset(in_stack, 0, sizeof(in_stack));

        int counter = 0, progress = 0;
        if (dfs_cycle(pid, visited, in_stack, &counter, &progress)) {
            for (int i = 0; i < MAX_PROCS; i++) {
                if (in_stack[i] && is_active(i)) {
                    if (out_victim < 0 ||
                        graph.priority[i] > graph.priority[out_victim] ||
                        (graph.priority[i] == graph.priority[out_victim] && i > out_victim)) {
                        out_victim = i;
                    }
                }
            }
        }
    }

    if (out_victim >= 0 && victim_pid) {
        *victim_pid = out_victim;
        return 1;
    }
    return 0;
}

void deadlock_resolve(int pid, int tick) {
    if (pid < 0 || pid >= MAX_PROCS) return;

    char data[128];
    snprintf(data, sizeof(data), "{\"victim_pid\":%d}", pid);
    logger_log(tick, "DEADLOCK", LOG_WARN, "Deadlock detected, killing victim", data);

    deadlock_cleanup_process(pid);

    for (int r = 0; r < NUM_RESOURCES; r++) {
        deadlock_wake_waiters(r, tick);
    }
}

void deadlock_cleanup_process(int pid) {
    if (pid < 0 || pid >= MAX_PROCS) return;

    for (int r = 0; r < NUM_RESOURCES; r++) {
        while (graph.allocation[pid][r] > 0) {
            deadlock_release(pid, r);
        }
        graph.request[pid][r] = 0;
    }
    graph.active[pid] = 0;
}

void deadlock_wake_waiters(int rid, int tick) {
    if (rid < 0 || rid >= NUM_RESOURCES) return;

    for (int i = 0; i < graph.wait_count[rid]; i++) {
        int wpid = graph.wait_pid[rid][i];
        if (wpid < 0) continue;
        if (!is_active(wpid)) {
            graph.wait_pid[rid][i] = -1;
            continue;
        }

        if (graph.available[rid] > 0) {
            deadlock_alloc(wpid, rid);
            graph.wait_pid[rid][i] = -1;
            if (victim_cb) {
                victim_cb(wpid, tick);
            }
        }
    }

    int w = 0;
    for (int i = 0; i < graph.wait_count[rid]; i++) {
        if (graph.wait_pid[rid][i] >= 0) {
            graph.wait_pid[rid][w++] = graph.wait_pid[rid][i];
        }
    }
    graph.wait_count[rid] = w;
}

const char* deadlock_resource_name(int rid) {
    switch (rid) {
        case RESOURCE_LANDING_PAD:    return "LandingPad";
        case RESOURCE_CHARGE_STATION: return "ChargeStation";
        case RESOURCE_COMM_CHANNEL:   return "CommChannel";
    }
    return "Unknown";
}
