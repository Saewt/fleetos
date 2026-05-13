#include <stdio.h>
#include <string.h>
#include "logger.h"

void logger_init(void) {
    setbuf(stdout, NULL);
}

const char* level_to_string(LogLevel level) {
    switch (level) {
        case LOG_DEBUG:    return "DEBUG";
        case LOG_INFO:     return "INFO";
        case LOG_WARN:     return "WARN";
        case LOG_ERROR:    return "ERROR";
        case LOG_CRITICAL: return "CRITICAL";
    }
    return "UNKNOWN";
}

void logger_log(int tick, const char *module, LogLevel level, const char *msg, const char *data_json) {
    if (data_json && strlen(data_json) > 0) {
        printf("{\"tick\":%d,\"module\":\"%s\",\"level\":\"%s\",\"msg\":\"%s\",\"data\":%s}\n",
               tick, module, level_to_string(level), msg, data_json);
    } else {
        printf("{\"tick\":%d,\"module\":\"%s\",\"level\":\"%s\",\"msg\":\"%s\"}\n",
               tick, module, level_to_string(level), msg);
    }
    fflush(stdout);
}

void logger_tick(int tick) {
    (void)tick;
}

void logger_snapshot(int tick, const char *procs_json) {
    logger_log(tick, "SNAPSHOT", LOG_INFO, "State snapshot", procs_json);
}
