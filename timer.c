#include "timer.h"
#include <stdio.h>
#include <math.h>

static uint64_t g_cntfrq_hz = 0;
static double g_tick_to_ns_factor = 0;
static double g_loop_overhead_ns = 0;

static uint64_t get_cntfrq(void) {
    uint64_t freq;
    asm volatile ("mrs %[freq], cntfrq_el0" : [freq] "=r" (freq));
    return freq;
}

static uint64_t get_cntvct(void) {
    uint64_t cnt;
    asm volatile ("isb\n mrs %[cnt], cntvct_el0\n isb" : [cnt] "=r" (cnt));
    return cnt;
}

static uint64_t get_cpu_frequency(void) {
    return 2200000000ULL;
}

void init_timer_backend(void) {
    g_cntfrq_hz = get_cntfrq();
    g_tick_to_ns_factor = 1e9 / (double)g_cntfrq_hz;
    
    uint64_t cpu_freq_hz = get_cpu_frequency();
    
    printf("=== System Counter Timer ===\n");
    printf("Method: cntvct_el0 (system counter)\n");
    printf("Resolution: %.2f ns per tick\n", g_tick_to_ns_factor);
    printf("Counter Frequency: %.2f MHz\n", g_cntfrq_hz / 1e6);
    printf("CPU Frequency: %.3f GHz\n", cpu_freq_hz / 1e9);
}

double get_time_ns(void) {
    uint64_t ticks = get_cntvct();
    return (double)ticks * g_tick_to_ns_factor;
}

void calibrate_loop_overhead(int iterations, int num_runs) {
    int unroll_factor = 32;
    uint64_t iter_count = iterations / unroll_factor;
    double overheads[num_runs];
    
    for (int run = 0; run < num_runs; run++) {
        asm volatile("isb" ::: "memory");
        double start = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        asm volatile (
            "mov x1, %[count]\n"
            "1:\n"
            "subs x1, x1, #1\n"
            "b.ne 1b\n"
            :
            : [count] "r" (iter_count)
            : "x1", "cc"
        );
        
        asm volatile("isb" ::: "memory");
        double end = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        overheads[run] = end - start;
    }
    
    double sum = 0;
    double min = overheads[0];
    double max = overheads[0];
    for (int i = 0; i < num_runs; i++) {
        sum += overheads[i];
        if (overheads[i] < min) min = overheads[i];
        if (overheads[i] > max) max = overheads[i];
    }
    double mean = sum / num_runs;
    
    double variance = 0;
    for (int i = 0; i < num_runs; i++) {
        variance += (overheads[i] - mean) * (overheads[i] - mean);
    }
    double stddev = sqrt(variance / (num_runs - 1));
    double sem = stddev / sqrt(num_runs);
    double error_pct = (sem / mean) * 100.0;
    
    g_loop_overhead_ns = mean;
    
    printf("\n=== Loop Overhead Calibration ===\n");
    printf("Overhead: %.3f ns (mean) ± %.3f ns (SEM)\n", mean, sem);
    printf("Per-iteration: %.6f ns\n", mean / iterations);
    printf("Error contribution: %.2f%%\n", error_pct);
}

double get_loop_overhead_ns(void) {
    return g_loop_overhead_ns;
}

double get_tick_to_ns_factor(void) {
    return g_tick_to_ns_factor;
}