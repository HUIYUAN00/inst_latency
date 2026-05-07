#include "test.h"
#include "benchmark.h"
#include <stdlib.h>
#include <stdio.h>

#define ARRAY_SIZE (1024 * 1024)

void warmup_cache(void) {
    volatile uint64_t *dummy = (uint64_t *)aligned_alloc(64, 1024 * 1024);
    if (!dummy) {
        fprintf(stderr, "Warning: warmup_cache allocation failed, test results may vary\n");
        return;
    }
    for (int i = 0; i < 131072; i++) {
        dummy[i] = i;
    }
    free((void *)dummy);
}

void test_ldr_throughput_optimized(int iterations, int num_runs, double loop_overhead_ns) {
    uint64_t *data = (uint64_t *)aligned_alloc(64, ARRAY_SIZE * sizeof(uint64_t));
    if (!data) {
        fprintf(stderr, "Error: aligned_alloc failed for LDR throughput test\n");
        exit(1);
    }
    
    for (int i = 0; i < ARRAY_SIZE; i++) {
        data[i] = (uint64_t)&data[(i + 8) & (ARRAY_SIZE - 1)];
    }
    
    run_performance_test("LDR Throughput", asm_ldr_throughput, (uint64_t)data, 
                         iterations, num_runs, loop_overhead_ns);
    free(data);
}

void test_ldr_latency_optimized(int iterations, int num_runs, double loop_overhead_ns) {
    uint64_t *data = (uint64_t *)aligned_alloc(64, ARRAY_SIZE * sizeof(uint64_t));
    if (!data) {
        fprintf(stderr, "Error: aligned_alloc failed for LDR latency test\n");
        exit(1);
    }
    
    for (int i = 0; i < ARRAY_SIZE - 1; i++) {
        data[i] = (uint64_t)&data[i + 1];
    }
    data[ARRAY_SIZE - 1] = (uint64_t)&data[0];
    
    run_performance_test("LDR Latency (Dependency Chain)", asm_ldr_latency, (uint64_t)data,
                         iterations, num_runs, loop_overhead_ns);
    free(data);
}

void test_fmla_latency_optimized(int iterations, int num_runs, double loop_overhead_ns) {
    run_performance_test("FMLA (SVE) Latency", asm_fmla_latency, 0,
                         iterations, num_runs, loop_overhead_ns);
}