#ifndef DRONE_H
#define DRONE_H

#include "common.h"
#include "pcb.h"

typedef enum {
    CMD_COMPUTE,
    CMD_PRODUCE_DATA,
    CMD_CONSUME_DATA,
    CMD_ACQUIRE_RESOURCE,
    CMD_IO_WRITE
} DroneCommand;

PCB* drone_create_flight(int pid);
PCB* drone_create_battery(int pid);
PCB* drone_create_mapping(int pid);
PCB* drone_create_log_collector(int pid);
void drone_execute(PCB *pcb);

#endif
