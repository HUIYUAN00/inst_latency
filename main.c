#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __ARM_FEATURE_SVE
#include <arm_sve.h>
#endif

#include "timer.h"
#include "test.h"

#define DEFAULT_ITERATIONS 10000000
#define DEFAULT_NUM_RUNS 10
#define MIN_ITERATIONS 1000
#define MAX_ITERATIONS 1000000000
#define MIN_RUNS 2
#define MAX_RUNS 1000

static int g_iterations = DEFAULT_ITERATIONS;
static int g_num_runs = DEFAULT_NUM_RUNS;

static void parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            int val = atoi(argv[i + 1]);
            if (val < MIN_ITERATIONS || val > MAX_ITERATIONS) {
                fprintf(stderr, "Error: iterations must be between %d and %d\n", 
                        MIN_ITERATIONS, MAX_ITERATIONS);
                exit(1);
            }
            g_iterations = val;
            i++;
        } else if (strcmp(argv[i], "--runs") == 0 && i + 1 < argc) {
            int val = atoi(argv[i + 1]);
            if (val < MIN_RUNS || val > MAX_RUNS) {
                fprintf(stderr, "Error: runs must be between %d and %d\n", 
                        MIN_RUNS, MAX_RUNS);
                exit(1);
            }
            g_num_runs = val;
            i++;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --iterations <num>  Set iterations per test (default: %d, range: %d-%d)\n", 
                   DEFAULT_ITERATIONS, MIN_ITERATIONS, MAX_ITERATIONS);
            printf("  --runs <num>        Set statistical runs (default: %d, range: %d-%d)\n", 
                   DEFAULT_NUM_RUNS, MIN_RUNS, MAX_RUNS);
            printf("  --help              Show this help\n");
            exit(0);
        }
    }
}

static void print_precision_summary(int iterations, int num_runs, double tick_to_ns_factor) {
    printf("\n=== Precision Analysis Summary ===\n");
    printf("Timer: System Counter (cntvct_el0)\n");
    printf("Resolution: %.2f ns per tick\n", tick_to_ns_factor);
    
    printf("\nMeasurement methodology:\n");
    printf("  Iterations: %d per test\n", iterations);
    printf("  Statistical runs: %d\n", num_runs);
    printf("  Effective resolution: %.6f ns\n", 
           tick_to_ns_factor / (double)iterations);
}

int main(int argc, char **argv) {
#ifndef __ARM_FEATURE_SVE
    printf("Error: SVE is not supported on this platform!\n");
    printf("Please compile with -march=armv8-a+sve flag\n");
    return 1;
#endif
    
    parse_args(argc, argv);
    
    printf("AArch64 SVE Instruction Latency Test\n");
    printf("====================================\n\n");
    
#ifdef __ARM_FEATURE_SVE
    printf("SVE Vector Length: %lu bits (%lu bytes)\n", 
           (unsigned long)svcntb() * 8, (unsigned long)svcntb());
#endif
    
    init_timer_backend();
    
    printf("\nTest Configuration:\n");
    printf("  Iterations: %d (unroll: 32)\n", g_iterations);
    printf("  Statistical runs: %d (with SEM error)\n", g_num_runs);
    printf("  Array size: %d KB\n", (1024 * 1024) * 8 / 1024);
    
    printf("\nInitializing...\n");
    warmup_cache();
    calibrate_loop_overhead(g_iterations, g_num_runs);
    
    double loop_overhead = get_loop_overhead_ns();
    
    test_ldr_throughput_optimized(g_iterations, g_num_runs, loop_overhead);
    test_ldr_latency_optimized(g_iterations, g_num_runs, loop_overhead);
    test_fmla_latency_optimized(g_iterations, g_num_runs, loop_overhead);
    
    print_precision_summary(g_iterations, g_num_runs, get_tick_to_ns_factor());
    
    printf("\n====================================\n");
    printf("Test completed!\n");
    printf("\nNote: Error margins (SEM) represent statistical uncertainty.\n");
    printf("      Sub-ns precision achieved via time accumulation.\n");
    
    return 0;
}