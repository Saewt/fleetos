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
#define TICK_INTERVAL_MS 500
#define LOG_LEVELS {"DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"}

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

#endif
