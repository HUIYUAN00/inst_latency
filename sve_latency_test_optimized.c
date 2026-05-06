#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>

#ifdef __ARM_FEATURE_SVE
#include <arm_sve.h>
#endif

#define ITERATIONS 10000000
#define UNROLL_FACTOR 32
#define NUM_RUNS 10
#define ARRAY_SIZE (1024 * 1024)
#define CALIBRATION_ITERATIONS 100000

typedef struct {
    const char *name;
    double precision_ns;
    uint64_t cpu_freq_hz;
} timer_info_t;

typedef struct {
    double mean;
    double stddev;
    double min;
    double max;
    double sem;
    double error_pct;
} stats_t;

static timer_info_t g_timer_info = {0};
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

static uint64_t calibrate_cpu_frequency(void) {
    uint64_t freq_khz = 0;
    
    FILE *fp = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
    if (fp) {
        if (fscanf(fp, "%lu", &freq_khz) == 1 && freq_khz > 0) {
            fclose(fp);
            return freq_khz * 1000ULL;
        }
        fclose(fp);
    }
    
    fp = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "r");
    if (fp) {
        if (fscanf(fp, "%lu", &freq_khz) == 1 && freq_khz > 0) {
            fclose(fp);
            return freq_khz * 1000ULL;
        }
        fclose(fp);
    }
    
    return 2200000000ULL;
}

static void init_timer_backend(void) {
    g_cntfrq_hz = get_cntfrq();
    g_tick_to_ns_factor = 1e9 / (double)g_cntfrq_hz;
    
    g_timer_info.cpu_freq_hz = calibrate_cpu_frequency();
    g_timer_info.name = "System Counter";
    g_timer_info.precision_ns = g_tick_to_ns_factor;
    
    printf("=== System Counter Timer ===\n");
    printf("Method: cntvct_el0 (system counter)\n");
    printf("Resolution: %.2f ns per tick\n", g_timer_info.precision_ns);
    printf("Counter Frequency: %.2f MHz\n", g_cntfrq_hz / 1e6);
    printf("CPU Frequency: %.3f GHz\n", g_timer_info.cpu_freq_hz / 1e9);
}

static double get_time_ns(void) {
    uint64_t ticks = get_cntvct();
    return (double)ticks * g_tick_to_ns_factor;
}

static stats_t compute_detailed_stats(double *values, int n) {
    stats_t s = {0};
    if (n == 0) return s;
    
    s.min = values[0];
    s.max = values[0];
    double sum = 0;
    
    for (int i = 0; i < n; i++) {
        sum += values[i];
        if (values[i] < s.min) s.min = values[i];
        if (values[i] > s.max) s.max = values[i];
    }
    s.mean = sum / n;
    
    double variance = 0;
    for (int i = 0; i < n; i++) {
        variance += (values[i] - s.mean) * (values[i] - s.mean);
    }
    s.stddev = sqrt(variance / (n - 1));
    
    s.sem = s.stddev / sqrt(n);
    
    s.error_pct = (s.sem / s.mean) * 100.0;
    
    return s;
}

static void warmup_cache(void) {
    volatile uint64_t *dummy = (uint64_t *)aligned_alloc(64, 1024 * 1024);
    if (dummy) {
        for (int i = 0; i < 131072; i++) {
            dummy[i] = i;
        }
        free((void *)dummy);
    }
}

static void calibrate_loop_overhead(void) {
    uint64_t iter_count = ITERATIONS / UNROLL_FACTOR;
    double overheads[NUM_RUNS];
    
    for (int run = 0; run < NUM_RUNS; run++) {
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
    
    stats_t s = compute_detailed_stats(overheads, NUM_RUNS);
    g_loop_overhead_ns = s.mean;
    
    printf("\n=== Loop Overhead Calibration ===\n");
    printf("Overhead: %.3f ns (mean) ± %.3f ns (SEM)\n", s.mean, s.sem);
    printf("Per-iteration: %.6f ns\n", s.mean / ITERATIONS);
    printf("Error contribution: %.2f%%\n", s.error_pct);
}

static void print_result_with_precision(const char *test_name, stats_t s) {
    double relative_error = s.error_pct;
    
    printf("\n=== %s ===\n", test_name);
    printf("Latency: %.3f ns (mean) ± %.3f ns (SEM)\n", s.mean, s.sem);
    printf("Range: [%.3f, %.3f] ns\n", s.min, s.max);
    printf("Relative error: ±%.2f%%\n", relative_error);
    
    if (relative_error > 10.0) {
        printf("WARNING: High measurement uncertainty (>10%%)\n");
    }
    
    printf("Throughput: %.2f M ops/sec\n", 1000.0 / s.mean);
}

static void test_ldr_throughput_optimized(void) {
    uint64_t *data = (uint64_t *)aligned_alloc(64, ARRAY_SIZE * sizeof(uint64_t));
    if (!data) {
        perror("aligned_alloc failed");
        return;
    }
    
    for (int i = 0; i < ARRAY_SIZE; i++) {
        data[i] = (uint64_t)&data[(i + 8) & (ARRAY_SIZE - 1)];
    }
    
    double results[NUM_RUNS];
    uint64_t iter_count = ITERATIONS / UNROLL_FACTOR;
    
    for (int run = 0; run < NUM_RUNS; run++) {
        asm volatile("isb" ::: "memory");
        double start = get_time_ns();
        asm volatile("isb" ::: "memory");
        
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
            "subs x1, x1, #1\n"
            "b.ne 1b\n"
            : 
            : [count] "r" (iter_count), [ptr] "r" (data)
            : "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
              "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18",
              "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27",
              "x28", "memory", "cc"
        );
        
        asm volatile("isb" ::: "memory");
        double end = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        double elapsed_ns = end - start;
        double adjusted_ns = elapsed_ns - g_loop_overhead_ns;
        results[run] = adjusted_ns / (double)ITERATIONS;
    }
    
    stats_t s = compute_detailed_stats(results, NUM_RUNS);
    print_result_with_precision("LDR Throughput", s);
    
    free(data);
}

static void test_ldr_latency_optimized(void) {
    uint64_t *data = (uint64_t *)aligned_alloc(64, ARRAY_SIZE * sizeof(uint64_t));
    if (!data) {
        perror("aligned_alloc failed");
        return;
    }
    
    for (int i = 0; i < ARRAY_SIZE - 1; i++) {
        data[i] = (uint64_t)&data[i + 1];
    }
    data[ARRAY_SIZE - 1] = (uint64_t)&data[0];
    
    double results[NUM_RUNS];
    uint64_t iter_count = ITERATIONS / UNROLL_FACTOR;
    uint64_t ptr_val = (uint64_t)data;
    
    for (int run = 0; run < NUM_RUNS; run++) {
        asm volatile("isb" ::: "memory");
        double start = get_time_ns();
        asm volatile("isb" ::: "memory");
        
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
            "subs x1, x1, #1\n"
            "b.ne 1b\n"
            : 
            : [count] "r" (iter_count), [ptr] "r" (ptr_val)
            : "x0", "x1", "memory", "cc"
        );
        
        asm volatile("isb" ::: "memory");
        double end = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        double elapsed_ns = end - start;
        double adjusted_ns = elapsed_ns - g_loop_overhead_ns;
        results[run] = adjusted_ns / (double)ITERATIONS;
    }
    
    stats_t s = compute_detailed_stats(results, NUM_RUNS);
    print_result_with_precision("LDR Latency (Dependency Chain)", s);
    
free(data);
}

static void test_fmla_latency_optimized(void) {
    double results[NUM_RUNS];
    uint64_t iter_count = ITERATIONS / UNROLL_FACTOR;
    
    for (int run = 0; run < NUM_RUNS; run++) {
        asm volatile("isb" ::: "memory");
        double start = get_time_ns();
        asm volatile("isb" ::: "memory");
        
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
        
        asm volatile("isb" ::: "memory");
        double end = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        double elapsed_ns = end - start;
        double adjusted_ns = elapsed_ns - g_loop_overhead_ns;
        results[run] = adjusted_ns / (double)ITERATIONS;
    }
    
    stats_t s = compute_detailed_stats(results, NUM_RUNS);
    print_result_with_precision("FMLA (SVE) Latency", s);
}

static void print_precision_summary(void) {
    printf("\n=== Precision Analysis Summary ===\n");
    printf("Timer: %s\n", g_timer_info.name);
    printf("Resolution: %.2f ns per tick\n", g_timer_info.precision_ns);
    printf("Counter Frequency: %.2f MHz\n", g_cntfrq_hz / 1e6);
    
    printf("\nMeasurement methodology:\n");
    printf("  Iterations: %d per test\n", ITERATIONS);
    printf("  Statistical runs: %d\n", NUM_RUNS);
    printf("  Effective resolution: %.6f ns\n", 
           g_timer_info.precision_ns / (double)ITERATIONS);
}

int main(void) {
#ifndef __ARM_FEATURE_SVE
    printf("Error: SVE is not supported on this platform!\n");
    printf("Please compile with -march=armv8-a+sve flag\n");
    return 1;
#endif
    
    printf("AArch64 SVE Instruction Latency Test (High-Precision)\n");
    printf("=====================================================\n\n");
    
#ifdef __ARM_FEATURE_SVE
    printf("SVE Vector Length: %lu bits (%lu bytes)\n", 
           (unsigned long)svcntb() * 8, (unsigned long)svcntb());
#endif
    
    init_timer_backend();
    
    printf("\nTest Configuration:\n");
    printf("  Iterations: %d (unroll: %d)\n", ITERATIONS, UNROLL_FACTOR);
    printf("  Statistical runs: %d (with SEM error)\n", NUM_RUNS);
    printf("  Array size: %d KB\n", ARRAY_SIZE * 8 / 1024);
    
    printf("\nInitializing...\n");
    warmup_cache();
    calibrate_loop_overhead();
    
    test_ldr_throughput_optimized();
    test_ldr_latency_optimized();
    test_fmla_latency_optimized();
    
    print_precision_summary();
    
    printf("\n=====================================================\n");
    printf("Test completed!\n");
    printf("\nNote: Error margins (SEM) represent statistical uncertainty.\n");
    printf("      Sub-ns precision achieved via time accumulation.\n");
    
    return 0;
}