#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include "kernel.h"
#include "logger.h"
#include "scheduler.h"
#include "memory.h"
#include "filesystem.h"
#include "deadlock.h"
#include "drone.h"
#include "sync.h"
#include "thread.h"

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
static int proc_count = 0;
static TCB *all_threads[MAX_THREADS];
static int thread_count = 0;
static PendingEvent pending_events[MAX_PROCS * 2];
static int pending_count = 0;
static BoundedBuffer sensor_buf;
static int io_seq = 0;

static PCB* find_proc(int pid) {
    for (int i = 0; i < proc_count; i++) {
        if (all_procs[i] && all_procs[i]->pid == pid) return all_procs[i];
    }
    return NULL;
}

static TCB* find_thread(int tid) {
    for (int i = 0; i < thread_count; i++) {
        if (all_threads[i] && all_threads[i]->tid == tid) return all_threads[i];
    }
    return NULL;
}

static int deadlock_phase[MAX_PROCS];
static int interactive_paused = 0;
static int step_target = 0;

static void sync_pcb_state_from_threads(PCB *pcb);

static void process_interactive(void) {
    fd_set fds;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 50000;

    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    while (interactive_paused || step_target <= 0) {
        if (step_target <= 0 && !interactive_paused) {
            interactive_paused = 1;
        }
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0) continue;

        char line[256];
        if (!fgets(line, sizeof(line), stdin)) continue;
        line[strcspn(line, "\n")] = 0;

        if (strcmp(line, "step") == 0) {
            step_target = 1;
            interactive_paused = 0;
        } else if (strncmp(line, "run ", 4) == 0) {
            int n = atoi(line + 4);
            if (n > 0) { step_target = n; interactive_paused = 0; }
        } else if (strcmp(line, "run") == 0) {
            step_target = 0;
            interactive_paused = 0;
        } else if (strcmp(line, "pause") == 0) {
            interactive_paused = 1;
            step_target = 0;
        } else if (strcmp(line, "status") == 0) {
            char snapshot[16384];
            size_t pos = 0;
            pos += snprintf(snapshot + pos, sizeof(snapshot) - pos, "{\"tick\":%d", current_tick);
            pos += snprintf(snapshot + pos, sizeof(snapshot) - pos, ",\"paused\":%s", interactive_paused ? "true" : "false");
            pos += snprintf(snapshot + pos, sizeof(snapshot) - pos, "}");
            printf("%s\n", snapshot);
            fflush(stdout);
        } else if (strcmp(line, "quit") == 0) {
            running = 0;
            return;
        }
    }
    if (step_target > 0) step_target--;
}

static void block_thread(TCB *tcb, BlockReason reason, int tick) {
    if (!tcb || tcb->state == T_BLOCKED) return;
    thread_block(tcb, reason);
    scheduler_block(tcb, tick);
    char tdata[256];
    snprintf(tdata, sizeof(tdata), "{\"tid\":%d,\"owner_pid\":%d,\"reason\":\"%s\"}",
             tcb->tid, tcb->owner_pid, block_reason_to_string(reason));
    logger_log(tick, "THREAD", LOG_INFO, "Thread blocked", tdata);

    PCB *pcb = find_proc(tcb->owner_pid);
    if (pcb) {
        sync_pcb_state_from_threads(pcb);
        char data[128];
        snprintf(data, sizeof(data), "{\"pid\":%d,\"reason\":\"%s\"}",
                 pcb->pid, block_reason_to_string(pcb->blocked_reason));
        logger_log(tick, "SCHED", LOG_INFO, "Process blocked", data);
    }
}

static void unblock_thread(TCB *tcb, int tick) {
    if (!tcb) return;
    thread_unblock(tcb);
    scheduler_unblock(tcb);
    char tdata[128];
    snprintf(tdata, sizeof(tdata), "{\"tid\":%d,\"owner_pid\":%d}",
             tcb->tid, tcb->owner_pid);
    logger_log(tick, "THREAD", LOG_INFO, "Thread unblocked", tdata);

    PCB *pcb = find_proc(tcb->owner_pid);
    if (pcb) {
        sync_pcb_state_from_threads(pcb);
        if (pcb->state == READY || pcb->state == RUNNING) {
            char data[64];
            snprintf(data, sizeof(data), "{\"pid\":%d}", pcb->pid);
            logger_log(tick, "SCHED", LOG_INFO, "Process unblocked", data);
        }
    }
}

static void sync_pcb_state_from_threads(PCB *pcb) {
    if (!pcb) return;
    if (pcb->state == TERMINATED || pcb->state == NEW) return;

    int running = 0, ready = 0, blocked_count = 0, live_count = 0;
    BlockReason first_block_reason = NONE;

    for (int t = 0; t < thread_count; t++) {
        TCB *tcb = all_threads[t];
        if (!tcb || tcb->owner_pid != pcb->pid) continue;
        if (tcb->state == T_DONE) continue;
        live_count++;
        if (tcb->state == T_RUNNING) running = 1;
        if (tcb->state == T_READY) ready = 1;
        if (tcb->state == T_BLOCKED) {
            blocked_count++;
            if (blocked_count == 1) first_block_reason = tcb->wait_reason;
        }
    }

    if (live_count == 0) return;

    if (running) {
        pcb_set_state(pcb, RUNNING);
        pcb->blocked_reason = NONE;
    } else if (ready) {
        pcb_set_state(pcb, READY);
        pcb->blocked_reason = NONE;
    } else {
        pcb_set_state(pcb, BLOCKED);
        pcb->blocked_reason = first_block_reason;
    }
}

static int process_has_live_threads(int pid) {
    for (int i = 0; i < thread_count; i++) {
        if (all_threads[i] && all_threads[i]->owner_pid == pid && all_threads[i]->state != T_DONE) {
            return 1;
        }
    }
    return 0;
}

static void cleanup_process(PCB *pcb, int tick, const char *reason, int release_resources) {
    if (!pcb) return;

    for (int i = 0; i < pending_count; i++) {
        if (pending_events[i].active && pending_events[i].pid == pcb->pid) {
            pending_events[i].active = 0;
        }
    }

    buffer_remove_pid(&sensor_buf, pcb->pid);
    buffer_release_mutex(&sensor_buf, pcb->pid);

    if (release_resources) {
        deadlock_cleanup_process(pcb->pid);
        for (int r = 0; r < NUM_RESOURCES; r++) {
            deadlock_wake_waiters(r, tick);
        }
        fs_cleanup_process(pcb->pid);
    }

    mem_free_process(pcb);
    pcb_set_state(pcb, TERMINATED);
    pcb->completion_tick = tick;

    for (int i = 0; i < thread_count; i++) {
        if (all_threads[i] && all_threads[i]->owner_pid == pcb->pid && all_threads[i]->state != T_DONE) {
            all_threads[i]->state = T_DONE;
            char tdata[256];
            snprintf(tdata, sizeof(tdata), "{\"tid\":%d,\"owner_pid\":%d,\"state\":\"T_DONE\"}",
                     all_threads[i]->tid, pcb->pid);
            logger_log(tick, "THREAD", LOG_INFO, "Thread terminated", tdata);
        }
    }

    char data[128];
    snprintf(data, sizeof(data), "{\"pid\":%d,\"name\":\"%s\"}", pcb->pid, pcb->name);
    int is_fault = (strcmp(reason, "Crash injected") == 0 || strncmp(reason, "Deadlock", 8) == 0);
    const char *module = is_fault ? "FAULT" : "KERNEL";
    LogLevel level = is_fault ? LOG_CRITICAL : LOG_INFO;
    logger_log(tick, module, level, reason, data);
}

static void deadlock_wake_callback(int pid, int tick) {
    for (int i = 0; i < thread_count; i++) {
        TCB *tcb = all_threads[i];
        if (tcb && tcb->owner_pid == pid && tcb->state == T_BLOCKED && tcb->wait_reason == RESOURCE_WAIT) {
            unblock_thread(tcb, tick);
            deadlock_phase[pid]++;
            tcb->local_pc = (tcb->local_pc + 1) % tcb->command_count;
            tcb->command_ticks_remaining = 1;
            tcb->burst_remaining--;
        }
    }
}

void kernel_init(KernelConfig cfg) {
    config = cfg;
    current_tick = 0;
    running = 1;
    proc_count = 0;
    thread_count = 0;
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
    proc_count = 4;

    CommandType worker_cmds[] = {CMD_COMPUTE, CMD_COMPUTE};
    int next_tid = 0;

    for (int i = 0; i < proc_count; i++) {
        if (!all_procs[i]) continue;
        all_procs[i]->arrival_time = drone_get_arrival_time(i);
        deadlock_set_priority(all_procs[i]->pid, all_procs[i]->priority);

        mem_allocate_page(all_procs[i], 0, NULL);
        mem_allocate_page(all_procs[i], 1, NULL);

        // Main thread
        TCB *main_tcb = thread_create(next_tid, all_procs[i]->pid,
            all_procs[i]->commands, all_procs[i]->command_count,
            all_procs[i]->burst_remaining);
        main_tcb->priority = all_procs[i]->priority;
        main_tcb->mlfq_level = all_procs[i]->mlfq_level;
        all_threads[next_tid] = main_tcb;
        int main_tid = next_tid;
        next_tid++;

        // Worker thread
        TCB *worker_tcb = thread_spawn(next_tid, all_procs[i]->pid,
            worker_cmds, 2, 10);
        worker_tcb->priority = all_procs[i]->priority;
        worker_tcb->mlfq_level = all_procs[i]->mlfq_level;
        all_threads[next_tid] = worker_tcb;
        next_tid++;

        if (all_procs[i]->arrival_time == 0) {
            pcb_set_state(all_procs[i], READY);
            scheduler_add(main_tcb);
            scheduler_add(worker_tcb);
        }

        char tdata[128];
        snprintf(tdata, sizeof(tdata), "{\"tid\":%d,\"owner_pid\":%d}",
                 main_tid, all_procs[i]->pid);
        logger_log(0, "THREAD", LOG_INFO, "Thread created", tdata);
        snprintf(tdata, sizeof(tdata), "{\"tid\":%d,\"owner_pid\":%d}",
                 worker_tcb->tid, all_procs[i]->pid);
        logger_log(0, "THREAD", LOG_INFO, "Thread created", tdata);
    }

    thread_count = next_tid;

    char config_json[256];
    snprintf(config_json, sizeof(config_json),
        "{\"mode\":\"%s\",\"max_ticks\":%d,\"deadlock\":%s,\"crash\":%s,\"compare\":%s,\"interactive\":%s}",
        sched_mode_to_string(config.sched_mode), config.max_ticks,
        config.deadlock_enabled ? "true" : "false",
        config.crash_enabled ? "true" : "false",
        config.compare_enabled ? "true" : "false",
        config.interactive ? "true" : "false");
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
            if (proc) {
                char write_data[64];
                snprintf(write_data, sizeof(write_data),
                    "drone=%d tick=%d sensor_data",
                    pending_events[i].pid, pending_events[i].completion_tick);
                fs_write(pending_events[i].path, write_data, (int)strlen(write_data));

                char data[256];
                snprintf(data, sizeof(data), "{\"pid\":%d,\"path\":\"%s\"}",
                         pending_events[i].pid, pending_events[i].path);
                logger_log(current_tick, "FS", LOG_INFO, "I/O completed", data);

                // Unblock all threads of this process blocked on IO
                for (int t = 0; t < thread_count; t++) {
                    TCB *tcb = all_threads[t];
                    if (tcb && tcb->owner_pid == pending_events[i].pid && tcb->state == T_BLOCKED && tcb->wait_reason == IO_BLOCK) {
                        unblock_thread(tcb, current_tick);
                    }
                }
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
                EvictionInfo ei = {0};
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
            if (proc) {
                for (int t = 0; t < thread_count; t++) {
                    TCB *tcb = all_threads[t];
                    if (tcb && tcb->owner_pid == proc->pid && tcb->state == T_BLOCKED && tcb->wait_reason == PAGE_FAULT) {
                        unblock_thread(tcb, current_tick);
                    }
                }
            }
            pending_events[i].active = 0;
        }
    }
}

static void wake_buffer_waiters(int tick) {
    int wpids[MAX_PROCS];
    int wcount;

    if (sensor_buf.count > 0) {
        wcount = cond_broadcast(&sensor_buf.not_empty, wpids, MAX_PROCS);
        for (int i = 0; i < wcount; i++) {
            TCB *tcb = find_thread(wpids[i]);
            if (tcb && tcb->state == T_BLOCKED) unblock_thread(tcb, tick);
        }
    }

    if (sensor_buf.count < RING_BUFFER_SIZE) {
        wcount = cond_broadcast(&sensor_buf.not_full, wpids, MAX_PROCS);
        for (int i = 0; i < wcount; i++) {
            TCB *tcb = find_thread(wpids[i]);
            if (tcb && tcb->state == T_BLOCKED) unblock_thread(tcb, tick);
        }
    }

    if (!sensor_buf.mutex.locked && sensor_buf.mutex.wait_count > 0) {
        int wpid = sensor_buf.mutex.wait_queue[0];
        for (int i = 1; i < sensor_buf.mutex.wait_count; i++) {
            sensor_buf.mutex.wait_queue[i - 1] = sensor_buf.mutex.wait_queue[i];
        }
        sensor_buf.mutex.wait_count--;
        TCB *tcb = find_thread(wpid);
        if (tcb && tcb->state == T_BLOCKED) unblock_thread(tcb, tick);
    }
}

static void trigger_page_fault(int tick) {
    if (config.deadlock_enabled) return;
    if (tick <= 0 || tick % PAGE_FAULT_INTERVAL != 0) return;

    TCB *current = scheduler_get_current();
    if (!current || current->state != T_RUNNING) return;

    PCB *proc = find_proc(current->owner_pid);
    if (!proc) return;

    int next_page = mem_get_page_count(proc);
    if (next_page >= MAX_PAGES) return;

    int fault_page;
    int result = mem_access(proc, next_page * PAGE_SIZE, &fault_page);
    if (result < 0) {
        char data[128];
        snprintf(data, sizeof(data), "{\"pid\":%d,\"page\":%d}",
                 proc->pid, fault_page);
        logger_log(tick, "MEM", LOG_WARN, "Page fault", data);

        block_thread(current, PAGE_FAULT, tick);

        for (int i = 0; i < pending_count; i++) {
            if (!pending_events[i].active) {
                pending_events[i].pid = proc->pid;
                pending_events[i].completion_tick = tick + PAGE_FAULT_DURATION;
                pending_events[i].reason = PAGE_FAULT;
                pending_events[i].page = fault_page;
                pending_events[i].active = 1;
                return;
            }
        }
        if (pending_count < MAX_PROCS * 2) {
            pending_events[pending_count].pid = proc->pid;
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

        if (config.interactive) {
            process_interactive();
            if (!running) break;
        }

        mem_set_tick(current_tick);
        deadlock_set_tick(current_tick);
        sync_set_tick(current_tick);

        /* 1. Check new arrivals */
        for (int i = 0; i < proc_count; i++) {
            if (all_procs[i]->state == NEW && all_procs[i]->arrival_time <= current_tick) {
                char data[128];
                snprintf(data, sizeof(data), "{\"pid\":%d,\"name\":\"%s\",\"priority\":\"%s\"}",
                         all_procs[i]->pid, all_procs[i]->name,
                         priority_to_string(all_procs[i]->priority));
                logger_log(current_tick, "KERNEL", LOG_INFO, "Process arrived", data);
                pcb_set_state(all_procs[i], READY);
                for (int t = 0; t < thread_count; t++) {
                    if (all_threads[t] && all_threads[t]->owner_pid == all_procs[i]->pid) {
                        scheduler_add(all_threads[t]);
                    }
                }
            }
        }

        /* 2. Complete pending I/O events */
        complete_io_events();

        /* 3. Complete pending page faults */
        complete_page_faults();

        compact_pending_events();

        /* 4. Ensure we have a running thread */
        TCB *current_thread = scheduler_get_current();
        if (!current_thread || current_thread->state != T_RUNNING) {
            scheduler_next(current_tick);
            current_thread = scheduler_get_current();
        }

        /* 5. Execute current thread command */
        if (current_thread && current_thread->state == T_RUNNING) {
            PCB *current_proc = find_proc(current_thread->owner_pid);
            CommandType cmd = thread_current_command(current_thread);

            switch (cmd) {
                case CMD_COMPUTE:
                    thread_execute(current_thread);
                    break;

                case CMD_PRODUCE_DATA: {
                    int item = current_tick * 100 + current_thread->owner_pid;
                    int wake_pids[MAX_WAKE_PIDS];
                    int wake_count;
                    if (buffer_produce(&sensor_buf, item, current_thread->owner_pid, wake_pids, &wake_count) < 0) {
                        block_thread(current_thread, MUTEX_WAIT, current_tick);
                    } else {
                        char data[128];
                        snprintf(data, sizeof(data), "{\"pid\":%d,\"item\":%d,\"buf_count\":%d}",
                                 current_thread->owner_pid, item, sensor_buf.count);
                        logger_log(current_tick, "SYNC", LOG_INFO, "Data produced", data);
                        thread_execute(current_thread);
                        for (int w = 0; w < wake_count; w++) {
                            TCB *wtcb = find_thread(wake_pids[w]);
                            if (wtcb && wtcb->state == T_BLOCKED) unblock_thread(wtcb, current_tick);
                        }
                    }
                    break;
                }

                case CMD_CONSUME_DATA: {
                    int item;
                    int wake_pids[MAX_WAKE_PIDS];
                    int wake_count;
                    if (buffer_consume(&sensor_buf, &item, current_thread->owner_pid, wake_pids, &wake_count) < 0) {
                        block_thread(current_thread, MUTEX_WAIT, current_tick);
                    } else {
                        char data[128];
                        snprintf(data, sizeof(data), "{\"pid\":%d,\"item\":%d,\"buf_count\":%d}",
                                 current_thread->owner_pid, item, sensor_buf.count);
                        logger_log(current_tick, "SYNC", LOG_INFO, "Data consumed", data);
                        thread_execute(current_thread);
                        for (int w = 0; w < wake_count; w++) {
                            TCB *wtcb = find_thread(wake_pids[w]);
                            if (wtcb && wtcb->state == T_BLOCKED) unblock_thread(wtcb, current_tick);
                        }
                    }
                    break;
                }

                case CMD_ACQUIRE_RESOURCE:
                    if (config.deadlock_enabled) {
                        int rid;
                        if (current_thread->owner_pid == 0) {
                            rid = (deadlock_phase[0] == 0) ? RESOURCE_LANDING_PAD : RESOURCE_CHARGE_STATION;
                        } else if (current_thread->owner_pid == 1) {
                            rid = (deadlock_phase[1] == 0) ? RESOURCE_CHARGE_STATION : RESOURCE_LANDING_PAD;
                        } else {
                            rid = RESOURCE_COMM_CHANNEL;
                        }
                        int result = deadlock_request(current_thread->owner_pid, rid);
                        if (result == 0) {
                            char data[128];
                            snprintf(data, sizeof(data), "{\"pid\":%d,\"rid\":%d,\"resource\":\"%s\"}",
                                     current_thread->owner_pid, rid, deadlock_resource_name(rid));
                            logger_log(current_tick, "DEADLOCK", LOG_INFO, "Resource acquired", data);
                            deadlock_phase[current_thread->owner_pid]++;
                            current_thread->local_pc = (current_thread->local_pc + 1) % current_thread->command_count;
                            current_thread->command_ticks_remaining = 1;
                            current_thread->burst_remaining--;
                        } else if (result == 1) {
                            char data[128];
                            snprintf(data, sizeof(data), "{\"pid\":%d,\"rid\":%d,\"resource\":\"%s\"}",
                                     current_thread->owner_pid, rid, deadlock_resource_name(rid));
                            logger_log(current_tick, "DEADLOCK", LOG_INFO, "Resource already held", data);
                            current_thread->local_pc = (current_thread->local_pc + 1) % current_thread->command_count;
                            current_thread->command_ticks_remaining = 1;
                            current_thread->burst_remaining--;
                        } else {
                            block_thread(current_thread, RESOURCE_WAIT, current_tick);
                        }
                    } else {
                        thread_execute(current_thread);
                    }
                    break;

                case CMD_IO_WRITE: {
                    char path[64];
                    snprintf(path, sizeof(path), "/logs/drone_%d_seq_%d.log",
                             current_thread->owner_pid, io_seq++);
                    fs_create(path);

                    char data[128];
                    snprintf(data, sizeof(data), "{\"pid\":%d,\"path\":\"%s\",\"duration\":%d}",
                             current_thread->owner_pid, path, IO_DURATION);
                    logger_log(current_tick, "FS", LOG_INFO, "I/O write started", data);

                    current_thread->local_pc = (current_thread->local_pc + 1) % current_thread->command_count;
                    current_thread->command_ticks_remaining = 1;
                    current_thread->burst_remaining--;

                    block_thread(current_thread, IO_BLOCK, current_tick);

                    for (int i = 0; i < MAX_PROCS * 2; i++) {
                        if (!pending_events[i].active) {
                            pending_events[i].pid = current_thread->owner_pid;
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

            if (current_thread->state == T_RUNNING && current_thread->burst_remaining <= 0) {
                scheduler_terminate(current_thread, current_tick);
                if (current_proc && !process_has_live_threads(current_proc->pid)) {
                    cleanup_process(current_proc, current_tick, "Process terminated", 0);
                }
            }
        }

        /* 5.5. Deadlock detection every 10 ticks */
        if (current_tick > 0 && current_tick % 10 == 0) {
            int victim;
            if (deadlock_detect(&victim)) {
                PCB *victim_proc = find_proc(victim);
                if (victim_proc) {
                    deadlock_resolve(victim, current_tick);
                    cleanup_process(victim_proc, current_tick, "Deadlock victim terminated", 0);
                }
            }
        }

        /* 5.6. Crash injection */
        if (config.crash_enabled && current_tick == 30) {
            TCB *crashed_thread = scheduler_get_current();
            if (crashed_thread && crashed_thread->state == T_RUNNING) {
                PCB *crashed_proc = find_proc(crashed_thread->owner_pid);
                if (crashed_proc) {
                    cleanup_process(crashed_proc, current_tick, "Crash injected", 1);
                }
            }
        }

        /* 6. Trigger page fault simulation */
        trigger_page_fault(current_tick);

        /* 7. Scheduler tick (quantum check) */
        scheduler_tick(current_tick);

        /* 8. Wait time tracking */
        for (int i = 0; i < proc_count; i++) {
            if (all_procs[i]->state == READY) {
                all_procs[i]->wait_time++;
            }
        }

        /* 9. JSON snapshot */
        if (current_tick % SNAPSHOT_INTERVAL == 0) {
            for (int i = 0; i < proc_count; i++) {
                if (all_procs[i]->state == TERMINATED) continue;
                sync_pcb_state_from_threads(all_procs[i]);
            }

            char snapshot[16384];
            size_t pos = 0;

            pos += snprintf(snapshot + pos, sizeof(snapshot) - pos, "{");

            pos += snprintf(snapshot + pos, sizeof(snapshot) - pos, "\"procs\":[");
            int added = 0;
            for (int i = 0; i < proc_count; i++) {
                if (all_procs[i]->state == TERMINATED) continue;
                if (added > 0) pos += snprintf(snapshot + pos, sizeof(snapshot) - pos, ",");
                char pcb_buf[2048];
                pcb_to_json_with_threads(all_procs[i], all_threads, thread_count, pcb_buf, sizeof(pcb_buf));
                pos += snprintf(snapshot + pos, sizeof(snapshot) - pos, "%s", pcb_buf);
                added++;
            }
            pos += snprintf(snapshot + pos, sizeof(snapshot) - pos, "]");

            char mem_buf[1024];
            mem_to_json(mem_buf, sizeof(mem_buf));
            pos += snprintf(snapshot + pos, sizeof(snapshot) - pos, ",\"memory\":{%s}", mem_buf);

            char sched_buf[1024];
            scheduler_to_json(sched_buf, sizeof(sched_buf));
            pos += snprintf(snapshot + pos, sizeof(snapshot) - pos, ",\"scheduler\":%s", sched_buf);

            char fs_buf[1024];
            fs_to_json(fs_buf, sizeof(fs_buf));
            pos += snprintf(snapshot + pos, sizeof(snapshot) - pos, ",\"filesystem\":{%s}", fs_buf);

            char buf_buf[512];
            buffer_to_json(&sensor_buf, buf_buf, sizeof(buf_buf));
            pos += snprintf(snapshot + pos, sizeof(snapshot) - pos, ",\"buffer\":%s", buf_buf);

            pos += snprintf(snapshot + pos, sizeof(snapshot) - pos, "}");

            logger_snapshot(current_tick, snapshot);
        }

        /* 10. Check if buffer waiters should be woken */
        wake_buffer_waiters(current_tick);

        /* 11. Termination check */
        int alive = 0;
        for (int i = 0; i < proc_count; i++) {
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

KernelMetrics kernel_get_metrics(void) {
    KernelMetrics m;
    memset(&m, 0, sizeof(m));
    m.mode = sched_mode_to_string(config.sched_mode);
    m.ticks_run = current_tick;
    m.context_switches = scheduler_get_context_switches();
    m.priority_boosts = scheduler_get_priority_boost_count();

    int total_wait = 0;
    int max_wait = 0;
    int term_count = 0;
    int total_turnaround = 0;
    int max_turnaround = 0;
    int turnaround_count = 0;
    for (int i = 0; i < proc_count; i++) {
        if (all_procs[i]->state == TERMINATED) term_count++;
        total_wait += all_procs[i]->wait_time;
        if (all_procs[i]->wait_time > max_wait) max_wait = all_procs[i]->wait_time;
        if (all_procs[i]->completion_tick >= 0) {
            int ta = all_procs[i]->completion_tick - all_procs[i]->arrival_time;
            total_turnaround += ta;
            if (ta > max_turnaround) max_turnaround = ta;
            turnaround_count++;
        }
    }
    m.total_wait = total_wait;
    m.max_wait = max_wait;
    m.terminated_count = term_count;
    m.avg_wait = (proc_count > 0) ? (double)total_wait / proc_count : 0.0;
    m.avg_turnaround = (turnaround_count > 0) ? (double)total_turnaround / turnaround_count : 0.0;
    m.max_turnaround = max_turnaround;
    return m;
}

void kernel_shutdown(void) {
    running = 0;
    mem_set_tick(current_tick);
    for (int i = 0; i < proc_count; i++) {
        mem_free_process(all_procs[i]);
        pcb_destroy(all_procs[i]);
    }
    for (int i = 0; i < thread_count; i++) {
        if (all_threads[i]) {
            free(all_threads[i]);
            all_threads[i] = NULL;
        }
    }
    proc_count = 0;
    thread_count = 0;
    logger_log(current_tick, "KERNEL", LOG_INFO, "Kernel shutdown", NULL);
}
