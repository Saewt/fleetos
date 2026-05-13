#ifndef DRONE_H
#define DRONE_H

#include "common.h"
#include "pcb.h"

#define FLIGHT_ARRIVAL   0
#define BATTERY_ARRIVAL  0
#define MAPPING_ARRIVAL  2
#define LOGCOLL_ARRIVAL  4

PCB* drone_create_flight(int pid);
PCB* drone_create_battery(int pid);
PCB* drone_create_mapping(int pid);
PCB* drone_create_log_collector(int pid);
void drone_execute(PCB *pcb);

int drone_get_arrival_time(int pid);

#endif
