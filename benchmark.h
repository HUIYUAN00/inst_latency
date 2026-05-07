#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdint.h>
#include "stats.h"

typedef void (*test_asm_func_t)(uint64_t iter_count, uint64_t param);

stats_t run_performance_test(const char *name, test_asm_func_t test_asm, uint64_t param, 
                             int iterations, int num_runs, double loop_overhead_ns);

void asm_ldr_throughput(uint64_t iter_count, uint64_t param);
void asm_ldr_latency(uint64_t iter_count, uint64_t param);
void asm_fmla_latency(uint64_t iter_count, uint64_t param);

#endif