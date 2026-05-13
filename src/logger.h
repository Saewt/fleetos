#ifndef LOGGER_H
#define LOGGER_H

#include "common.h"

void logger_init(void);
void logger_log(int tick, const char *module, LogLevel level, const char *msg, const char *data_json);
void logger_tick(int tick);
void logger_snapshot(int tick, const char *procs_json);
const char* level_to_string(LogLevel level);

#endif
