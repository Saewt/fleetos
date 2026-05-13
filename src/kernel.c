#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "kernel.h"
#include "logger.h"
#include "scheduler.h"
#include "memory.h"
#include "filesystem.h"
#include "deadlock.h"
#include "drone.h"

static KernelConfig config;
static int current_tick = 0;
static int running = 1;
static PCB *all_procs[MAX_PROCS];
static int all_count = 0;

void kernel_init(KernelConfig cfg) {
    config = cfg;
    current_tick = 0;
    running = 1;
    all_count = 0;

    logger_init();
    scheduler_init(config.sched_mode);
    mem_init();
    fs_init();
    deadlock_init();

    all_procs[0] = drone_create_flight(0);
    all_procs[1] = drone_create_battery(1);
    all_procs[2] = drone_create_mapping(2);
    all_procs[3] = drone_create_log_collector(3);
    all_count = 4;

    for (int i = 0; i < all_count; i++) {
        if (!all_procs[i]) continue;
        all_procs[i]->arrival_time = drone_get_arrival_time(i);
        if (all_procs[i]->arrival_time == 0) {
            pcb_set_state(all_procs[i], READY);
            scheduler_add(all_procs[i]);
        }
    }

    char config_json[256];
    snprintf(config_json, sizeof(config_json),
        "{\"mode\":\"%s\",\"max_ticks\":%d,\"deadlock\":%s,\"crash\":%s,\"compare\":%s}",
        sched_mode_to_string(config.sched_mode), config.max_ticks,
        config.deadlock_enabled ? "true" : "false",
        config.crash_enabled ? "true" : "false",
        config.compare_enabled ? "true" : "false");
    logger_log(0, "KERNEL", LOG_INFO, "Simulation started", config_json);
}

void kernel_run(void) {
    while (running) {
        if (config.max_ticks > 0 && current_tick >= config.max_ticks) {
            logger_log(current_tick, "KERNEL", LOG_INFO, "Simulation completed", NULL);
            break;
        }

        if (config.max_ticks <= 0) {
            usleep(TICK_INTERVAL_MS * 1000);
        }

        /* 1. Check new arrivals */
        for (int i = 0; i < all_count; i++) {
            if (all_procs[i]->state == NEW && all_procs[i]->arrival_time <= current_tick) {
                char data[128];
                snprintf(data, sizeof(data), "{\"pid\":%d,\"name\":\"%s\",\"priority\":\"%s\"}",
                         all_procs[i]->pid, all_procs[i]->name,
                         priority_to_string(all_procs[i]->priority));
                logger_log(current_tick, "KERNEL", LOG_INFO, "Process arrived", data);
                scheduler_add(all_procs[i]);
            }
        }

        /* 2. Complete I/O operations — Phase 1 */
        /* 3. Complete page fault handling — Phase 1 */
        /* 4. Run deadlock detection every 10 ticks — Phase 1 */

        /* 5. Execute current process command */
        PCB *current = scheduler_get_current();
        if (current && current->state == RUNNING) {
            drone_execute(current);

            if (current->burst_remaining <= 0) {
                pcb_set_state(current, TERMINATED);
                char data[128];
                snprintf(data, sizeof(data), "{\"pid\":%d,\"name\":\"%s\"}",
                         current->pid, current->name);
                logger_log(current_tick, "KERNEL", LOG_INFO, "Process terminated", data);
                scheduler_block(current);
            }
        }

        /* 6. Scheduler tick (quantum check) */
        scheduler_tick(current_tick);

        /* 7. JSON state snapshot every SNAPSHOT_INTERVAL ticks */
        if (current_tick % SNAPSHOT_INTERVAL == 0) {
            char procs_json[4096] = "[";
            int added = 0;
            for (int i = 0; i < all_count; i++) {
                if (all_procs[i]->state == TERMINATED) continue;
                if (added > 0) strcat(procs_json, ",");
                strcat(procs_json, pcb_to_json(all_procs[i]));
                added++;
            }
            strcat(procs_json, "]");
            logger_snapshot(current_tick, procs_json);
        }

        /* 8. Termination check */
        int alive = 0;
        for (int i = 0; i < all_count; i++) {
            if (all_procs[i]->state != TERMINATED) {
                alive++;
            }
        }
        if (alive == 0 && config.max_ticks <= 0) {
            logger_log(current_tick, "KERNEL", LOG_INFO, "All processes terminated", NULL);
            break;
        }

        current_tick++;
    }
}

void kernel_shutdown(void) {
    running = 0;
    for (int i = 0; i < all_count; i++) {
        pcb_destroy(all_procs[i]);
    }
    all_count = 0;
    logger_log(current_tick, "KERNEL", LOG_INFO, "Kernel shutdown", NULL);
}
