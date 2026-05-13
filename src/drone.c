#include <stdlib.h>
#include <string.h>
#include "drone.h"
#include "logger.h"

PCB* drone_create_flight(int pid) {
    PCB *pcb = pcb_create(pid, "Flight", HIGH);
    /* TODO: Setup command sequence: COMPUTE, PRODUCE_DATA, ACQUIRE_RESOURCE */
    (void)pcb;
    return pcb;
}

PCB* drone_create_battery(int pid) {
    PCB *pcb = pcb_create(pid, "Battery", CRITICAL);
    /* TODO: Setup command sequence: COMPUTE, ACQUIRE_RESOURCE, IO_WRITE */
    (void)pcb;
    return pcb;
}

PCB* drone_create_mapping(int pid) {
    PCB *pcb = pcb_create(pid, "Mapping", NORMAL);
    /* TODO: Setup command sequence: COMPUTE, CONSUME_DATA, IO_WRITE */
    (void)pcb;
    return pcb;
}

PCB* drone_create_log_collector(int pid) {
    PCB *pcb = pcb_create(pid, "LogCollector", LOW);
    /* TODO: Setup command sequence: CONSUME_DATA, IO_WRITE */
    (void)pcb;
    return pcb;
}

void drone_execute(PCB *pcb) {
    (void)pcb;
    /* TODO: Execute current drone command */
}
