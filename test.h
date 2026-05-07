#ifndef TEST_H
#define TEST_H

#include <stdint.h>

void warmup_cache(void);
void test_ldr_throughput_optimized(int iterations, int num_runs, double loop_overhead_ns);
void test_ldr_latency_optimized(int iterations, int num_runs, double loop_overhead_ns);
void test_fmla_latency_optimized(int iterations, int num_runs, double loop_overhead_ns);

#endif