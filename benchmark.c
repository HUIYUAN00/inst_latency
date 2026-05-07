#include "benchmark.h"
#include "timer.h"
#include <stdlib.h>

#define UNROLL_FACTOR 32

stats_t run_performance_test(const char *name, test_asm_func_t test_asm, uint64_t param,
                             int iterations, int num_runs, double loop_overhead_ns) {
    uint64_t iter_count = iterations / UNROLL_FACTOR;
    double results[num_runs];
    
    for (int run = 0; run < num_runs; run++) {
        asm volatile("isb" ::: "memory");
        double start = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        test_asm(iter_count, param);
        
        asm volatile("isb" ::: "memory");
        double end = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        double elapsed_ns = end - start;
        double adjusted_ns = elapsed_ns - loop_overhead_ns;
        results[run] = adjusted_ns / (double)iterations;
    }
    
    stats_t s = compute_detailed_stats(results, num_runs);
    print_result_with_precision(name, s);
    return s;
}

void asm_ldr_throughput(uint64_t iter_count, uint64_t param) {
    uint64_t *data = (uint64_t *)param;
    asm volatile (
        "mov x1, %[count]\n"
        "mov x2, %[ptr]\n"
        "1:\n"
        "ldr x3, [x2]\n"
        "ldr x4, [x2, #8]\n"
        "ldr x5, [x2, #16]\n"
        "ldr x6, [x2, #24]\n"
        "ldr x7, [x2, #32]\n"
        "ldr x8, [x2, #40]\n"
        "ldr x9, [x2, #48]\n"
        "ldr x10, [x2, #56]\n"
        "ldr x11, [x2, #64]\n"
        "ldr x12, [x2, #72]\n"
        "ldr x13, [x2, #80]\n"
        "ldr x14, [x2, #88]\n"
        "ldr x15, [x2, #96]\n"
        "ldr x16, [x2, #104]\n"
        "ldr x17, [x2, #112]\n"
        "ldr x18, [x2, #120]\n"
        "ldr x19, [x2, #128]\n"
        "ldr x20, [x2, #136]\n"
        "ldr x21, [x2, #144]\n"
        "ldr x22, [x2, #152]\n"
        "ldr x23, [x2, #160]\n"
        "ldr x24, [x2, #168]\n"
        "ldr x25, [x2, #176]\n"
        "ldr x26, [x2, #184]\n"
        "ldr x27, [x2, #192]\n"
        "ldr x28, [x2, #200]\n"
        "ldr x3, [x2, #208]\n"
        "ldr x4, [x2, #216]\n"
        "ldr x5, [x2, #224]\n"
        "ldr x6, [x2, #232]\n"
        "ldr x7, [x2, #240]\n"
        "ldr x8, [x2, #248]\n"
        "subs x1, x1, #1\n"
        "b.ne 1b\n"
        : 
        : [count] "r" (iter_count), [ptr] "r" (data)
        : "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
          "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18",
          "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27",
          "x28", "memory", "cc"
    );
}

void asm_ldr_latency(uint64_t iter_count, uint64_t param) {
    asm volatile (
        "mov x1, %[count]\n"
        "mov x0, %[ptr]\n"
        "1:\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "ldr x0, [x0]\n"
        "subs x1, x1, #1\n"
        "b.ne 1b\n"
        : 
        : [count] "r" (iter_count), [ptr] "r" (param)
        : "x0", "x1", "memory", "cc"
    );
}

void asm_fmla_latency(uint64_t iter_count, uint64_t param) {
    (void)param;
    asm volatile (
        "mov x0, %[count]\n"
        "ptrue p0.s\n"
        "fmov z0.s, #1.0\n"
        "fmov z1.s, #2.0\n"
        "fmov z2.s, #3.0\n"
        "1:\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "fmla z0.s, p0/m, z1.s, z2.s\n"
        "subs x0, x0, #1\n"
        "b.ne 1b\n"
        : 
        : [count] "r" (iter_count)
        : "x0", "p0", "z0", "z1", "z2", "cc"
    );
}