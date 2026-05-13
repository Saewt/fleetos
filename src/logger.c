#include <stdio.h>
#include <string.h>
#include "logger.h"

void logger_init(void) {
    /* TODO: Initialize logging system */
}

void logger_log(int tick, const char *module, LogLevel level, const char *msg, const char *data_json) {
    (void)level;
    if (data_json && strlen(data_json) > 0) {
        printf("{\"tick\":%d,\"module\":\"%s\",\"level\":\"INFO\",\"msg\":\"%s\",\"data\":%s}\n", tick, module, msg, data_json);
    } else {
        printf("{\"tick\":%d,\"module\":\"%s\",\"level\":\"INFO\",\"msg\":\"%s\"}\n", tick, module, msg);
    }
}

void logger_tick(int tick) {
    (void)tick;
    /* TODO: Flush or periodic snapshot */
}
