#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include "common.h"
#include "kernel.h"

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [OPTIONS]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --mode rr|mlfq    Scheduling mode (default: rr)\n");
    fprintf(stderr, "  --ticks N         Number of ticks to simulate (default: infinite)\n");
    fprintf(stderr, "  --deadlock        Enable deadlock scenario\n");
    fprintf(stderr, "  --crash           Enable crash scenario\n");
    fprintf(stderr, "  --compare         Run both RR and MLFQ for comparison\n");
    fprintf(stderr, "  --interactive     Interactive mode (stdin control)\n");
    fprintf(stderr, "  --help            Show this help message\n");
}

int main(int argc, char *argv[]) {
    KernelConfig config = {
        .sched_mode = SCHED_RR,
        .max_ticks = 0,
        .deadlock_enabled = 0,
        .crash_enabled = 0,
        .compare_enabled = 0,
        .interactive = 0
    };

    static struct option long_options[] = {
        {"mode",     required_argument, 0, 'm'},
        {"ticks",    required_argument, 0, 't'},
        {"deadlock", no_argument,       0, 'd'},
        {"crash",    no_argument,       0, 'c'},
        {"compare",  no_argument,       0, 'p'},
        {"interactive", no_argument,    0, 'i'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:t:dcpih", long_options, NULL)) != -1) {
        switch (opt) {
            case 'm':
                if (strcmp(optarg, "rr") == 0) {
                    config.sched_mode = SCHED_RR;
                } else if (strcmp(optarg, "mlfq") == 0) {
                    config.sched_mode = SCHED_MLFQ;
                } else {
                    fprintf(stderr, "Invalid mode: %s (use 'rr' or 'mlfq')\n", optarg);
                    return 1;
                }
                break;
            case 't':
                config.max_ticks = atoi(optarg);
                if (config.max_ticks <= 0) {
                    fprintf(stderr, "Ticks must be a positive integer\n");
                    return 1;
                }
                break;
            case 'd':
                config.deadlock_enabled = 1;
                break;
            case 'c':
                config.crash_enabled = 1;
                break;
            case 'p':
                config.compare_enabled = 1;
                break;
            case 'i':
                config.interactive = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    KernelMetrics m1, m2;

    if (config.compare_enabled) {
        config.sched_mode = SCHED_RR;
        kernel_init(config);
        kernel_run();
        m1 = kernel_get_metrics();
        kernel_shutdown();

        config.sched_mode = SCHED_MLFQ;
        kernel_init(config);
        kernel_run();
        m2 = kernel_get_metrics();
        kernel_shutdown();

        printf("{\"comparison\":{"
               "\"rr\":{\"ticks\":%d,\"context_switches\":%d,\"prio_boosts\":%d,\"total_wait\":%d,\"avg_wait\":%.2f,\"max_wait\":%d,\"terminated\":%d,\"avg_turnaround\":%.2f,\"max_turnaround\":%d},"
               "\"mlfq\":{\"ticks\":%d,\"context_switches\":%d,\"prio_boosts\":%d,\"total_wait\":%d,\"avg_wait\":%.2f,\"max_wait\":%d,\"terminated\":%d,\"avg_turnaround\":%.2f,\"max_turnaround\":%d}"
               "}}\n",
               m1.ticks_run, m1.context_switches, m1.priority_boosts, m1.total_wait, m1.avg_wait, m1.max_wait, m1.terminated_count, m1.avg_turnaround, m1.max_turnaround,
               m2.ticks_run, m2.context_switches, m2.priority_boosts, m2.total_wait, m2.avg_wait, m2.max_wait, m2.terminated_count, m2.avg_turnaround, m2.max_turnaround);
    } else {
        kernel_init(config);
        kernel_run();
        kernel_shutdown();
    }

    (void)m1;
    (void)m2;
    return 0;
}
