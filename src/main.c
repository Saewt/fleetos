#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "kernel.h"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("FleetOS: Drone Fleet OS Simulator\n");
    printf("Usage: %s [--mode rr|mlfq] [--deadlock] [--crash] [--compare]\n", argv[0]);

    kernel_init();
    /* TODO: Parse CLI arguments and run appropriate scenario */
    kernel_run();
    kernel_shutdown();

    return 0;
}
