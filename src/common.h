#ifndef COMMON_H
#define COMMON_H

#define MAX_PROCS 16
#define MAX_THREADS 32
#define MAX_RESOURCES 8
#define PAGE_SIZE 256
#define FRAME_COUNT 16
#define MAX_PAGES 16
#define BLOCK_SIZE 64
#define BLOCK_COUNT 64
#define MAX_FILES 16
#define MAX_COMMANDS 32
#define TICK_INTERVAL_MS 500
#define SNAPSHOT_INTERVAL 5
#define SCHED_QUANTUM_RR 4
#define SCHED_QUANTUM_MLFQ_Q0 2
#define SCHED_QUANTUM_MLFQ_Q1 4
#define SCHED_QUANTUM_MLFQ_Q2 8
#define IO_DURATION 3
#define PAGE_FAULT_DURATION 3
#define BUFFER_SIZE 8
#define PAGE_FAULT_INTERVAL 8
#define RING_BUFFER_SIZE 8

typedef enum {
    NEW, READY, RUNNING, BLOCKED, TERMINATED, SUSPENDED
} ProcessState;

typedef enum {
    CRITICAL, HIGH, NORMAL, LOW
} Priority;

typedef enum {
    NONE, IO_BLOCK, PAGE_FAULT, MUTEX_WAIT, RESOURCE_WAIT
} BlockReason;

typedef enum {
    LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_CRITICAL
} LogLevel;

typedef enum {
    CMD_COMPUTE,
    CMD_PRODUCE_DATA,
    CMD_CONSUME_DATA,
    CMD_ACQUIRE_RESOURCE,
    CMD_IO_WRITE
} CommandType;

typedef enum {
    SCHED_RR,
    SCHED_MLFQ
} SchedMode;

typedef struct {
    SchedMode sched_mode;
    int max_ticks;
    int deadlock_enabled;
    int crash_enabled;
    int compare_enabled;
} KernelConfig;

const char* state_to_string(ProcessState s);
const char* priority_to_string(Priority p);
const char* block_reason_to_string(BlockReason r);
const char* command_to_string(CommandType c);
const char* sched_mode_to_string(SchedMode m);

#endif
