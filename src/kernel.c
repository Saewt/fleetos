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
#include "sync.h"

typedef struct {
    int pid;
    int completion_tick;
    BlockReason reason;
    char path[64];
    int page;
    int active;
} PendingEvent;

static KernelConfig config;
static int current_tick = 0;
static int running = 1;
static PCB *all_procs[MAX_PROCS];
static int all_count = 0;
static PendingEvent pending_events[MAX_PROCS * 2];
static int pending_count = 0;
static BoundedBuffer sensor_buf;
static int io_seq = 0;

static PCB* find_proc(int pid) {
    for (int i = 0; i < all_count; i++) {
        if (all_procs[i] && all_procs[i]->pid == pid) return all_procs[i];
    }
    return NULL;
}

static int deadlock_phase[MAX_PROCS];

static void block_process(PCB *pcb, BlockReason reason, int tick) {
    if (!pcb || pcb->state == BLOCKED) return;
    pcb->blocked_reason = reason;
    scheduler_block(pcb, tick);
    char data[128];
    snprintf(data, sizeof(data), "{\"pid\":%d,\"reason\":\"%s\"}",
             pcb->pid, block_reason_to_string(reason));
    logger_log(tick, "SCHED", LOG_INFO, "Process blocked", data);
}

static void unblock_process(PCB *pcb, int tick) {
    if (!pcb) return;
    pcb->blocked_reason = NONE;
    scheduler_unblock(pcb);
    char data[64];
    snprintf(data, sizeof(data), "{\"pid\":%d}", pcb->pid);
    logger_log(tick, "SCHED", LOG_INFO, "Process unblocked", data);
}

static void deadlock_wake_callback(int pid, int tick) {
    PCB *proc = find_proc(pid);
    if (proc && proc->state == BLOCKED && proc->blocked_reason == RESOURCE_WAIT) {
        unblock_process(proc, tick);
    }
}

static void crash_process(PCB *pcb, int tick) {
    if (!pcb) return;
    char data[128];
    snprintf(data, sizeof(data), "{\"pid\":%d,\"name\":\"%s\"}",
             pcb->pid, pcb->name);
    logger_log(tick, "FAULT", LOG_CRITICAL, "Drone crash injected", data);

    deadlock_cleanup_process(pcb->pid);
    mem_free_process(pcb);
    pcb_set_state(pcb, TERMINATED);
    scheduler_block(pcb, tick);
}

void kernel_init(KernelConfig cfg) {
    config = cfg;
    current_tick = 0;
    running = 1;
    all_count = 0;
    pending_count = 0;
    io_seq = 0;
    memset(pending_events, 0, sizeof(pending_events));

    logger_init();
    scheduler_init(config.sched_mode);
    mem_init();
    fs_init();
    deadlock_init();
    deadlock_set_victim_callback(deadlock_wake_callback);
    buffer_init(&sensor_buf);

    memset(deadlock_phase, 0, sizeof(deadlock_phase));

    all_procs[0] = drone_create_flight(0);
    all_procs[1] = drone_create_battery(1);
    all_procs[2] = drone_create_mapping(2);
    all_procs[3] = drone_create_log_collector(3);
    all_count = 4;

    for (int i = 0; i < all_count; i++) {
        if (!all_procs[i]) continue;
        all_procs[i]->arrival_time = drone_get_arrival_time(i);

        mem_allocate_page(all_procs[i], 0, NULL);
        mem_allocate_page(all_procs[i], 1, NULL);

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

static void compact_pending_events(void) {
    int write = 0;
    for (int read = 0; read < pending_count; read++) {
        if (pending_events[read].active) {
            if (write != read) {
                pending_events[write] = pending_events[read];
            }
            write++;
        }
    }
    pending_count = write;
}

static void complete_io_events(void) {
    for (int i = 0; i < pending_count; i++) {
        if (!pending_events[i].active) continue;
        if (pending_events[i].reason != IO_BLOCK) continue;
        if (current_tick >= pending_events[i].completion_tick) {
            PCB *proc = find_proc(pending_events[i].pid);
            if (proc && proc->blocked_reason == IO_BLOCK) {
                char write_data[64];
                snprintf(write_data, sizeof(write_data),
                    "drone=%d tick=%d sensor_data",
                    proc->pid, pending_events[i].completion_tick);
                fs_write(pending_events[i].path, write_data, (int)strlen(write_data));

                char data[256];
                snprintf(data, sizeof(data), "{\"pid\":%d,\"path\":\"%s\"}",
                         pending_events[i].pid, pending_events[i].path);
                logger_log(current_tick, "FS", LOG_INFO, "I/O completed", data);
                unblock_process(proc, current_tick);
            }
            pending_events[i].active = 0;
        }
    }
}

static void complete_page_faults(void) {
    for (int i = 0; i < pending_count; i++) {
        if (!pending_events[i].active) continue;
        if (pending_events[i].reason != PAGE_FAULT) continue;
        if (current_tick >= pending_events[i].completion_tick) {
            PCB *proc = find_proc(pending_events[i].pid);
            if (proc) {
                EvictionInfo ei;
                mem_allocate_page(proc, pending_events[i].page, &ei);
                if (ei.evicted) {
                    PCB *victim = find_proc(ei.victim_pid);
                    if (victim && ei.victim_page >= 0) {
                        victim->page_table[ei.victim_page] = -1;
                        victim->pages_used--;
                    }
                }
                logger_log(current_tick, "MEM", LOG_INFO, "Page fault resolved", NULL);
            }
            if (proc && proc->blocked_reason == PAGE_FAULT) {
                unblock_process(proc, current_tick);
            }
            pending_events[i].active = 0;
        }
    }
}

static void trigger_page_fault(int tick) {
    if (tick <= 0 || tick % PAGE_FAULT_INTERVAL != 0) return;

    PCB *current = scheduler_get_current();
    if (!current || current->state != RUNNING) return;

    int next_page = mem_get_page_count(current);
    if (next_page >= MAX_PAGES) return;

    int fault_page;
    int result = mem_access(current, next_page * PAGE_SIZE, &fault_page);
    if (result < 0) {
        char data[128];
        snprintf(data, sizeof(data), "{\"pid\":%d,\"page\":%d}",
                 current->pid, fault_page);
        logger_log(tick, "MEM", LOG_WARN, "Page fault", data);

        block_process(current, PAGE_FAULT, tick);

        for (int i = 0; i < pending_count; i++) {
            if (!pending_events[i].active) {
                pending_events[i].pid = current->pid;
                pending_events[i].completion_tick = tick + PAGE_FAULT_DURATION;
                pending_events[i].reason = PAGE_FAULT;
                pending_events[i].page = fault_page;
                pending_events[i].active = 1;
                return;
            }
        }
        if (pending_count < MAX_PROCS * 2) {
            pending_events[pending_count].pid = current->pid;
            pending_events[pending_count].completion_tick = tick + PAGE_FAULT_DURATION;
            pending_events[pending_count].reason = PAGE_FAULT;
            pending_events[pending_count].page = fault_page;
            pending_events[pending_count].active = 1;
            pending_count++;
        }
    }
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

        mem_set_tick(current_tick);
        deadlock_set_tick(current_tick);

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

        /* 2. Complete pending I/O events */
        complete_io_events();

        /* 3. Complete pending page faults */
        complete_page_faults();

        compact_pending_events();

        /* 4. Ensure we have a running process */
        PCB *current = scheduler_get_current();
        if (!current || current->state != RUNNING) {
            scheduler_next(current_tick);
            current = scheduler_get_current();
        }

        /* 5. Execute current process command */
        if (current && current->state == RUNNING) {
            CommandType cmd = current->commands[current->program_counter % current->command_count];

            switch (cmd) {
                case CMD_COMPUTE:
                    drone_execute(current);
                    break;

                case CMD_PRODUCE_DATA: {
                    int item = current_tick * 100 + current->pid;
                    if (buffer_produce(&sensor_buf, item) < 0) {
                        block_process(current, MUTEX_WAIT, current_tick);
                    } else {
                        char data[128];
                        snprintf(data, sizeof(data), "{\"pid\":%d,\"item\":%d,\"buf_count\":%d}",
                                 current->pid, item, sensor_buf.count);
                        logger_log(current_tick, "SYNC", LOG_INFO, "Data produced", data);
                        drone_execute(current);
                    }
                    break;
                }

                case CMD_CONSUME_DATA: {
                    int item;
                    if (buffer_consume(&sensor_buf, &item) < 0) {
                        block_process(current, MUTEX_WAIT, current_tick);
                    } else {
                        char data[128];
                        snprintf(data, sizeof(data), "{\"pid\":%d,\"item\":%d,\"buf_count\":%d}",
                                 current->pid, item, sensor_buf.count);
                        logger_log(current_tick, "SYNC", LOG_INFO, "Data consumed", data);
                        drone_execute(current);
                    }
                    break;
                }

                case CMD_ACQUIRE_RESOURCE:
                    if (config.deadlock_enabled) {
                        int rid;
                        if (current->pid == 0) {
                            rid = (deadlock_phase[0] == 0) ? RESOURCE_LANDING_PAD : RESOURCE_CHARGE_STATION;
                        } else if (current->pid == 1) {
                            rid = (deadlock_phase[1] == 0) ? RESOURCE_CHARGE_STATION : RESOURCE_LANDING_PAD;
                        } else {
                            rid = RESOURCE_COMM_CHANNEL;
                        }
                        int result = deadlock_request(current->pid, rid);
                        if (result < 0) {
                            block_process(current, RESOURCE_WAIT, current_tick);
                        } else {
                            char data[128];
                            snprintf(data, sizeof(data), "{\"pid\":%d,\"rid\":%d,\"resource\":\"%s\"}",
                                     current->pid, rid, deadlock_resource_name(rid));
                            logger_log(current_tick, "DEADLOCK", LOG_INFO, "Resource acquired", data);
                            deadlock_phase[current->pid]++;
                            current->program_counter = (current->program_counter + 1) % current->command_count;
                            current->command_ticks_remaining = 1;
                            current->burst_remaining--;
                        }
                    } else {
                        drone_execute(current);
                    }
                    break;

                case CMD_IO_WRITE: {
                    char path[64];
                    snprintf(path, sizeof(path), "/logs/drone_%d_seq_%d.log",
                             current->pid, io_seq++);
                    fs_create(path);

                    char data[128];
                    snprintf(data, sizeof(data), "{\"pid\":%d,\"path\":\"%s\",\"duration\":%d}",
                             current->pid, path, IO_DURATION);
                    logger_log(current_tick, "FS", LOG_INFO, "I/O write started", data);

                    current->program_counter = (current->program_counter + 1) % current->command_count;
                    current->command_ticks_remaining = 1;
                    current->burst_remaining--;

                    block_process(current, IO_BLOCK, current_tick);

                    for (int i = 0; i < MAX_PROCS * 2; i++) {
                        if (!pending_events[i].active) {
                            pending_events[i].pid = current->pid;
                            pending_events[i].completion_tick = current_tick + IO_DURATION;
                            pending_events[i].reason = IO_BLOCK;
                            strncpy(pending_events[i].path, path, 63);
                            pending_events[i].path[63] = '\0';
                            pending_events[i].active = 1;
                            if (i >= pending_count) pending_count = i + 1;
                            break;
                        }
                    }
                    break;
                }
            }

            if (current->state == RUNNING && current->burst_remaining <= 0) {
                pcb_set_state(current, TERMINATED);
                char data[128];
                snprintf(data, sizeof(data), "{\"pid\":%d,\"name\":\"%s\"}",
                         current->pid, current->name);
                logger_log(current_tick, "KERNEL", LOG_INFO, "Process terminated", data);
                scheduler_block(current, current_tick);
            }
        }

        /* 5.5. Deadlock detection every 10 ticks */
        if (current_tick > 0 && current_tick % 10 == 0) {
            int victim;
            if (deadlock_detect(&victim)) {
                PCB *victim_proc = find_proc(victim);
                if (victim_proc) {
                    crash_process(victim_proc, current_tick);
                }
            }
        }

        /* 5.6. Crash injection */
        if (config.crash_enabled && current_tick == 30) {
            PCB *current = scheduler_get_current();
            if (current && current->state == RUNNING) {
                crash_process(current, current_tick);
            }
        }

        /* 6. Trigger page fault simulation */
        trigger_page_fault(current_tick);

        /* 7. Scheduler tick (quantum check) */
        scheduler_tick(current_tick);

        /* 8. Wait time tracking */
        for (int i = 0; i < all_count; i++) {
            if (all_procs[i]->state == READY) {
                all_procs[i]->wait_time++;
            }
        }

        /* 9. JSON snapshot */
        if (current_tick % SNAPSHOT_INTERVAL == 0) {
            char procs_json[8192];
            size_t pos = 0;
            pos += snprintf(procs_json + pos, sizeof(procs_json) - pos, "[");
            int added = 0;
            for (int i = 0; i < all_count; i++) {
                if (all_procs[i]->state == TERMINATED) continue;
                if (added > 0) pos += snprintf(procs_json + pos, sizeof(procs_json) - pos, ",");
                char pcb_buf[1024];
                pcb_to_json(all_procs[i], pcb_buf, sizeof(pcb_buf));
                pos += snprintf(procs_json + pos, sizeof(procs_json) - pos, "%s", pcb_buf);
                added++;
            }
            pos += snprintf(procs_json + pos, sizeof(procs_json) - pos, "]");
            logger_snapshot(current_tick, procs_json);
        }

        /* 10. Check if consumer blocked on empty buffer should be woken */
        for (int i = 0; i < all_count; i++) {
            if (all_procs[i]->blocked_reason == MUTEX_WAIT && sensor_buf.count > 0) {
                CommandType cmd = all_procs[i]->commands[all_procs[i]->program_counter % all_procs[i]->command_count];
                if (cmd == CMD_CONSUME_DATA) {
                    unblock_process(all_procs[i], current_tick);
                }
            }
            if (all_procs[i]->blocked_reason == MUTEX_WAIT && sensor_buf.count < RING_BUFFER_SIZE) {
                CommandType cmd = all_procs[i]->commands[all_procs[i]->program_counter % all_procs[i]->command_count];
                if (cmd == CMD_PRODUCE_DATA) {
                    unblock_process(all_procs[i], current_tick);
                }
            }
        }

        /* 11. Termination check */
        int alive = 0;
        for (int i = 0; i < all_count; i++) {
            if (all_procs[i]->state != TERMINATED) {
                alive++;
            }
        }
        if (alive == 0) {
            logger_log(current_tick, "KERNEL", LOG_INFO, "All processes terminated", NULL);
            break;
        }

        current_tick++;
    }
}

void kernel_shutdown(void) {
    running = 0;
    mem_set_tick(current_tick);
    for (int i = 0; i < all_count; i++) {
        mem_free_process(all_procs[i]);
        pcb_destroy(all_procs[i]);
    }
    all_count = 0;
    logger_log(current_tick, "KERNEL", LOG_INFO, "Kernel shutdown", NULL);
}
