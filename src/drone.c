#include <stdlib.h>
#include <string.h>
#include "drone.h"

static CommandType flight_cmds[]  = {CMD_COMPUTE, CMD_PRODUCE_DATA, CMD_ACQUIRE_RESOURCE};
static CommandType battery_cmds[] = {CMD_COMPUTE, CMD_ACQUIRE_RESOURCE, CMD_IO_WRITE};
static CommandType mapping_cmds[] = {CMD_COMPUTE, CMD_CONSUME_DATA, CMD_IO_WRITE};
static CommandType log_cmds[]     = {CMD_CONSUME_DATA, CMD_IO_WRITE};

static void drone_set_commands(PCB *pcb, CommandType *cmds, int count, int burst) {
    pcb->command_count = count;
    for (int i = 0; i < count && i < MAX_COMMANDS; i++) {
        pcb->commands[i] = cmds[i];
    }
    pcb->program_counter = 0;
    pcb->command_ticks_remaining = 2;  /* COMPUTE and others take 2 ticks */
    pcb->burst_remaining = burst;
}

PCB* drone_create_flight(int pid) {
    PCB *pcb = pcb_create(pid, "Flight", HIGH);
    if (!pcb) return NULL;
    drone_set_commands(pcb, flight_cmds, 3, 20);
    return pcb;
}

PCB* drone_create_battery(int pid) {
    PCB *pcb = pcb_create(pid, "Battery", CRITICAL);
    if (!pcb) return NULL;
    drone_set_commands(pcb, battery_cmds, 3, 25);
    return pcb;
}

PCB* drone_create_mapping(int pid) {
    PCB *pcb = pcb_create(pid, "Mapping", NORMAL);
    if (!pcb) return NULL;
    drone_set_commands(pcb, mapping_cmds, 3, 15);
    return pcb;
}

PCB* drone_create_log_collector(int pid) {
    PCB *pcb = pcb_create(pid, "LogCollector", LOW);
    if (!pcb) return NULL;
    drone_set_commands(pcb, log_cmds, 2, 10);
    return pcb;
}

void drone_execute(PCB *pcb) {
    if (!pcb || pcb->state != RUNNING) return;
    if (pcb->burst_remaining <= 0) return;

    pcb->command_ticks_remaining--;
    if (pcb->command_ticks_remaining <= 0) {
        pcb->program_counter = (pcb->program_counter + 1) % pcb->command_count;
        pcb->command_ticks_remaining = 1;
    }
    pcb->burst_remaining--;
}

int drone_get_arrival_time(int pid) {
    switch (pid) {
        case 0: return FLIGHT_ARRIVAL;
        case 1: return BATTERY_ARRIVAL;
        case 2: return MAPPING_ARRIVAL;
        case 3: return LOGCOLL_ARRIVAL;
    }
    return 0;
}
