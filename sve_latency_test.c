#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <time.h>
#include <math.h>

#ifdef __ARM_FEATURE_SVE
#include <arm_sve.h>
#endif

#define ITERATIONS 10000000
#define UNROLL_FACTOR 32
#define NUM_RUNS 5
#define ARRAY_SIZE (1024 * 1024)

static uint64_t g_loop_overhead = 0;
static uint64_t g_cpu_freq_hz = 0;
static uint64_t g_cntfrq_hz = 0;
static uint64_t g_timer_resolution_ns = 0;
static int g_use_pmu = 0;
static int g_pmu_enabled = 0;

static uint64_t get_cntfrq(void) {
    uint64_t freq;
    asm volatile (
        "isb\n"
        "mrs %[freq], cntfrq_el0\n"
        : [freq] "=r" (freq)
    );
    return freq;
}

static uint64_t get_cntvct(void) {
    uint64_t cnt;
    asm volatile (
        "isb\n"
        "mrs %[cnt], cntvct_el0\n"
        "isb\n"
        : [cnt] "=r" (cnt)
    );
    return cnt;
}

static int try_enable_pmu(void) {
    uint64_t pmcr = 0;
    uint64_t pmuseren = 0;
    
    asm volatile ("mrs %[pmuseren], pmuserenr_el0" : [pmuseren] "=r" (pmuseren));
    
    if ((pmuseren & 0x1) == 0) {
        return 0;
    }
    
    asm volatile ("mrs %[pmcr], pmcr_el0" : [pmcr] "=r" (pmcr));
    pmcr |= (1 << 0) | (1 << 2);
    asm volatile (
        "msr pmcr_el0, %[pmcr]\n"
        "msr pmcntenset_el0, %[enable]\n"
        "isb\n"
        :
        : [pmcr] "r" (pmcr), [enable] "r" (0x80000000ULL)
    );
    
    uint64_t cycles_before = 0, cycles_after = 0;
    asm volatile (
        "isb\n"
        "mrs %[cycles], pmccntr_el0\n"
        "isb\n"
        : [cycles] "=r" (cycles_before)
    );
    
    for (volatile int i = 0; i < 1000; i++) { }
    
    asm volatile (
        "isb\n"
        "mrs %[cycles], pmccntr_el0\n"
        "isb\n"
        : [cycles] "=r" (cycles_after)
    );
    
    if (cycles_after > cycles_before) {
        return 1;
    }
    
    return 0;
}

static uint64_t detect_cpu_frequency(void) {
    FILE *fp = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
    if (fp) {
        uint64_t freq_khz;
        if (fscanf(fp, "%lu", &freq_khz) == 1) {
            fclose(fp);
            return freq_khz * 1000ULL;
        }
        fclose(fp);
    }
    
    fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "BogoMIPS")) {
                double bogomips;
                if (sscanf(line, "BogoMIPS : %lf", &bogomips) == 1) {
                    fclose(fp);
                    return (uint64_t)(bogomips * 1000000ULL);
                }
            }
        }
        fclose(fp);
    }
    
    return 2200000000ULL;
}

static uint64_t get_time_ns_from_counter(void) {
    uint64_t cnt = get_cntvct();
    uint64_t freq = g_cntfrq_hz;
    return (cnt / freq) * 1000000000ULL + ((cnt % freq) * 1000000000ULL) / freq;
}

static uint64_t get_cycles_to_ns(uint64_t cycles) {
    if (g_cpu_freq_hz == 0) return 0;
    return (cycles * 1000000000ULL) / g_cpu_freq_hz;
}

static uint64_t get_pmu_cycles(void) {
    uint64_t cycles;
    asm volatile (
        "isb\n"
        "mrs %[cycles], pmccntr_el0\n"
        "isb\n"
        : [cycles] "=r" (cycles)
    );
    return cycles;
}

static uint64_t get_time_ns(void) {
    if (g_use_pmu && g_pmu_enabled) {
        return get_cycles_to_ns(get_pmu_cycles());
    } else {
        return get_time_ns_from_counter();
    }
}

static void init_timer_system(void) {
    g_cntfrq_hz = get_cntfrq();
    g_timer_resolution_ns = 1000000000ULL / g_cntfrq_hz;
    g_cpu_freq_hz = detect_cpu_frequency();
    
    g_pmu_enabled = try_enable_pmu();
    
    if (g_pmu_enabled) {
        g_use_pmu = 1;
        printf("PMU enabled: CPU cycles counter accessible\n");
        printf("Timer Resolution: ~%.3f ns (CPU cycle based)\n", 
               1000.0 / (g_cpu_freq_hz / 1e6));
    } else {
        g_use_pmu = 0;
        printf("PMU not accessible (insufficient permissions)\n");
        printf("Using system counter fallback\n");
        printf("Timer Resolution: %lu ns per tick\n", g_timer_resolution_ns);
    }
    
    printf("CPU Frequency: %.2f GHz\n", g_cpu_freq_hz / 1e9);
}

static void measure_loop_overhead(void) {
    uint64_t iter_count = ITERATIONS / UNROLL_FACTOR;
    uint64_t total = 0;
    
    for (int i = 0; i < NUM_RUNS; i++) {
        asm volatile("isb" ::: "memory");
        uint64_t start = get_time_ns();
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
        uint64_t end = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        total += (end - start);
    }
    
    g_loop_overhead = total / NUM_RUNS;
}

static void warmup_cache(void) {
    volatile uint64_t *dummy = (uint64_t *)malloc(1024 * 1024);
    if (dummy) {
        for (int i = 0; i < 131072; i++) {
            dummy[i] = i;
        }
        free((void *)dummy);
    }
}

typedef struct {
    double mean;
    double stddev;
    double min;
    double max;
} stats_t;

static stats_t compute_stats(double *values, int n) {
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
    
    return s;
}

static void test_ldr_throughput(void) {
    printf("=== Testing LDR Throughput ===\n");
    
    uint64_t *data = (uint64_t *)aligned_alloc(64, ARRAY_SIZE * sizeof(uint64_t));
    
    if (!data) {
        perror("aligned_alloc failed");
        return;
    }
    for (int i = 0; i < ARRAY_SIZE; i++) {
        data[i] = (uint64_t)&data[(i + 8) & (ARRAY_SIZE - 1)];
    }
    
    uint64_t loop_overhead = g_loop_overhead;
    double results[NUM_RUNS];
    
    for (int run = 0; run < NUM_RUNS; run++) {
        uint64_t iter_count = ITERATIONS / UNROLL_FACTOR;
        
        asm volatile("isb" ::: "memory");
        uint64_t start = get_time_ns();
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
        uint64_t end = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        double elapsed_ns = (double)(end - start - loop_overhead);
        results[run] = elapsed_ns / (double)ITERATIONS;
    }
    
    stats_t s = compute_stats(results, NUM_RUNS);
    printf("Latency (throughput): %.3f ns (mean), %.3f ns (stddev)\n", s.mean, s.stddev);
    printf("Throughput: %.2f M ops/sec\n", 1000.0 / s.mean);
    
    free(data);
}

static void test_ldr_latency(void) {
    printf("\n=== Testing LDR Latency (Dependency Chain) ===\n");
    
    uint64_t *data = (uint64_t *)aligned_alloc(64, ARRAY_SIZE * sizeof(uint64_t));
    
    if (!data) {
        perror("aligned_alloc failed");
        return;
    }
    for (int i = 0; i < ARRAY_SIZE - 1; i++) {
        data[i] = (uint64_t)&data[i + 1];
    }
    data[ARRAY_SIZE - 1] = (uint64_t)&data[0];
    
    uint64_t loop_overhead = g_loop_overhead;
    double results[NUM_RUNS];
    
    for (int run = 0; run < NUM_RUNS; run++) {
        uint64_t iter_count = ITERATIONS / UNROLL_FACTOR;
        uint64_t ptr_val = (uint64_t)data;
        
        asm volatile("isb" ::: "memory");
        uint64_t start = get_time_ns();
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
            "ldr x0, [x0]\n"
            "subs x1, x1, #1\n"
            "b.ne 1b\n"
            : 
            : [count] "r" (iter_count), [ptr] "r" (ptr_val)
            : "x0", "x1", "memory", "cc"
        );
        
        asm volatile("isb" ::: "memory");
        uint64_t end = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        double elapsed_ns = (double)(end - start - loop_overhead);
        results[run] = elapsed_ns / (double)ITERATIONS;
    }
    
    stats_t s = compute_stats(results, NUM_RUNS);
    printf("Latency (dependency chain): %.3f ns (mean), %.3f ns (stddev)\n", s.mean, s.stddev);
    printf("Throughput: %.2f M ops/sec\n", 1000.0 / s.mean);
    
    free(data);
}

static void test_str_throughput(void) {
    printf("\n=== Testing STR Throughput ===\n");
    
    uint64_t *data = (uint64_t *)aligned_alloc(64, ARRAY_SIZE * sizeof(uint64_t));
    
    if (!data) {
        perror("aligned_alloc failed");
        return;
    }
    uint64_t value = 0xDEADBEEF;
    
    uint64_t loop_overhead = g_loop_overhead;
    double results[NUM_RUNS];
    
    for (int run = 0; run < NUM_RUNS; run++) {
        uint64_t iter_count = ITERATIONS / UNROLL_FACTOR;
        
        asm volatile("isb" ::: "memory");
        uint64_t start = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        asm volatile (
            "mov x1, %[count]\n"
            "mov x2, %[val]\n"
            "1:\n"
            "str x2, [%[ptr]]\n"
            "str x2, [%[ptr], #8]\n"
            "str x2, [%[ptr], #16]\n"
            "str x2, [%[ptr], #24]\n"
            "str x2, [%[ptr], #32]\n"
            "str x2, [%[ptr], #40]\n"
            "str x2, [%[ptr], #48]\n"
            "str x2, [%[ptr], #56]\n"
            "str x2, [%[ptr], #64]\n"
            "str x2, [%[ptr], #72]\n"
            "str x2, [%[ptr], #80]\n"
            "str x2, [%[ptr], #88]\n"
            "str x2, [%[ptr], #96]\n"
            "str x2, [%[ptr], #104]\n"
            "str x2, [%[ptr], #112]\n"
            "str x2, [%[ptr], #120]\n"
            "str x2, [%[ptr], #128]\n"
            "str x2, [%[ptr], #136]\n"
            "str x2, [%[ptr], #144]\n"
            "str x2, [%[ptr], #152]\n"
            "str x2, [%[ptr], #160]\n"
            "str x2, [%[ptr], #168]\n"
            "str x2, [%[ptr], #176]\n"
            "str x2, [%[ptr], #184]\n"
            "str x2, [%[ptr], #192]\n"
            "str x2, [%[ptr], #200]\n"
            "str x2, [%[ptr], #208]\n"
            "str x2, [%[ptr], #216]\n"
            "str x2, [%[ptr], #224]\n"
            "str x2, [%[ptr], #232]\n"
            "str x2, [%[ptr], #240]\n"
            "str x2, [%[ptr], #248]\n"
            "subs x1, x1, #1\n"
            "b.ne 1b\n"
            : [ptr] "+r" (data)
            : [count] "r" (iter_count), [val] "r" (value)
            : "x1", "x2", "memory", "cc"
        );
        
        asm volatile("isb" ::: "memory");
        uint64_t end = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        double elapsed_ns = (double)(end - start - loop_overhead);
        results[run] = elapsed_ns / (double)ITERATIONS;
    }
    
    stats_t s = compute_stats(results, NUM_RUNS);
    printf("Latency (throughput): %.3f ns (mean), %.3f ns (stddev)\n", s.mean, s.stddev);
    printf("Throughput: %.2f M ops/sec\n", 1000.0 / s.mean);
    
    free(data);
}

static void test_ld1w_throughput(void) {
    printf("\n=== Testing LD1W (SVE) Throughput ===\n");
    
    float *data = (float *)aligned_alloc(64, ARRAY_SIZE * sizeof(float));
    
    if (!data) {
        perror("aligned_alloc failed");
        return;
    }
    for (int i = 0; i < ARRAY_SIZE; i++) {
        data[i] = (float)i;
    }
    
    uint64_t loop_overhead = g_loop_overhead;
    double results[NUM_RUNS];
    
    for (int run = 0; run < NUM_RUNS; run++) {
        uint64_t iter_count = ITERATIONS / UNROLL_FACTOR;
        
        asm volatile("isb" ::: "memory");
        uint64_t start = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        asm volatile (
            "mov x1, %[count]\n"
            "mov x2, %[ptr]\n"
            "add x2, x2, #256\n"
            "ptrue p0.s\n"
            "1:\n"
            "ld1w z0.s, p0/z, [x2]\n"
            "ld1w z1.s, p0/z, [x2, #1, MUL VL]\n"
            "ld1w z2.s, p0/z, [x2, #2, MUL VL]\n"
            "ld1w z3.s, p0/z, [x2, #3, MUL VL]\n"
            "ld1w z4.s, p0/z, [x2, #4, MUL VL]\n"
            "ld1w z5.s, p0/z, [x2, #5, MUL VL]\n"
            "ld1w z6.s, p0/z, [x2, #6, MUL VL]\n"
            "ld1w z7.s, p0/z, [x2, #7, MUL VL]\n"
            "ld1w z8.s, p0/z, [x2, #-8, MUL VL]\n"
            "ld1w z9.s, p0/z, [x2, #-7, MUL VL]\n"
            "ld1w z10.s, p0/z, [x2, #-6, MUL VL]\n"
            "ld1w z11.s, p0/z, [x2, #-5, MUL VL]\n"
            "ld1w z12.s, p0/z, [x2, #-4, MUL VL]\n"
            "ld1w z13.s, p0/z, [x2, #-3, MUL VL]\n"
            "ld1w z14.s, p0/z, [x2, #-2, MUL VL]\n"
            "ld1w z15.s, p0/z, [x2, #-1, MUL VL]\n"
            "ld1w z16.s, p0/z, [x2]\n"
            "ld1w z17.s, p0/z, [x2, #1, MUL VL]\n"
            "ld1w z18.s, p0/z, [x2, #2, MUL VL]\n"
            "ld1w z19.s, p0/z, [x2, #3, MUL VL]\n"
            "ld1w z20.s, p0/z, [x2, #4, MUL VL]\n"
            "ld1w z21.s, p0/z, [x2, #5, MUL VL]\n"
            "ld1w z22.s, p0/z, [x2, #6, MUL VL]\n"
            "ld1w z23.s, p0/z, [x2, #7, MUL VL]\n"
            "ld1w z24.s, p0/z, [x2, #-8, MUL VL]\n"
            "ld1w z25.s, p0/z, [x2, #-7, MUL VL]\n"
            "ld1w z26.s, p0/z, [x2, #-6, MUL VL]\n"
            "ld1w z27.s, p0/z, [x2, #-5, MUL VL]\n"
            "ld1w z28.s, p0/z, [x2, #-4, MUL VL]\n"
            "ld1w z29.s, p0/z, [x2, #-3, MUL VL]\n"
            "ld1w z30.s, p0/z, [x2, #-2, MUL VL]\n"
            "ld1w z31.s, p0/z, [x2, #-1, MUL VL]\n"
            "subs x1, x1, #1\n"
            "b.ne 1b\n"
            : 
            : [count] "r" (iter_count), [ptr] "r" (data)
            : "x1", "x2", "p0", "z0", "z1", "z2", "z3", "z4", "z5", 
              "z6", "z7", "z8", "z9", "z10", "z11", "z12", 
              "z13", "z14", "z15", "z16", "z17", "z18", "z19",
              "z20", "z21", "z22", "z23", "z24", "z25", "z26",
              "z27", "z28", "z29", "z30", "z31", "memory", "cc"
        );
        
        asm volatile("isb" ::: "memory");
        uint64_t end = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        double elapsed_ns = (double)(end - start - loop_overhead);
        results[run] = elapsed_ns / (double)ITERATIONS;
    }
    
    stats_t s = compute_stats(results, NUM_RUNS);
    printf("Latency (throughput): %.3f ns (mean), %.3f ns (stddev)\n", s.mean, s.stddev);
    printf("Throughput: %.2f M ops/sec\n", 1000.0 / s.mean);
    
    free(data);
}

static void test_st1w_throughput(void) {
    printf("\n=== Testing ST1W (SVE) Throughput ===\n");
    
    float *data = (float *)aligned_alloc(64, ARRAY_SIZE * sizeof(float));
    if (!data) {
        perror("aligned_alloc failed");
        return;
    }
    
    uint64_t loop_overhead = g_loop_overhead;
    double results[NUM_RUNS];
    
    for (int run = 0; run < NUM_RUNS; run++) {
        uint64_t iter_count = ITERATIONS / UNROLL_FACTOR;
        
        asm volatile("isb" ::: "memory");
        uint64_t start = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        asm volatile (
            "mov x1, %[count]\n"
            "mov x2, %[ptr]\n"
            "add x2, x2, #256\n"
            "ptrue p0.s\n"
            "fmov z0.s, #1.0\n"
            "fmov z1.s, #2.0\n"
            "fmov z2.s, #3.0\n"
            "fmov z3.s, #4.0\n"
            "fmov z4.s, #5.0\n"
            "fmov z5.s, #6.0\n"
            "fmov z6.s, #7.0\n"
            "fmov z7.s, #8.0\n"
            "fmov z8.s, #9.0\n"
            "fmov z9.s, #10.0\n"
            "fmov z10.s, #11.0\n"
            "fmov z11.s, #12.0\n"
            "fmov z12.s, #13.0\n"
            "fmov z13.s, #14.0\n"
            "fmov z14.s, #15.0\n"
            "fmov z15.s, #16.0\n"
            "fmov z16.s, #17.0\n"
            "fmov z17.s, #18.0\n"
            "fmov z18.s, #19.0\n"
            "fmov z19.s, #20.0\n"
            "fmov z20.s, #21.0\n"
            "fmov z21.s, #22.0\n"
            "fmov z22.s, #23.0\n"
            "fmov z23.s, #24.0\n"
            "fmov z24.s, #25.0\n"
            "fmov z25.s, #26.0\n"
            "fmov z26.s, #27.0\n"
            "fmov z27.s, #28.0\n"
            "fmov z28.s, #29.0\n"
            "fmov z29.s, #30.0\n"
            "fmov z30.s, #31.0\n"
            "fmov z31.s, #1.0\n"
            "1:\n"
            "st1w z0.s, p0, [x2]\n"
            "st1w z1.s, p0, [x2, #1, MUL VL]\n"
            "st1w z2.s, p0, [x2, #2, MUL VL]\n"
            "st1w z3.s, p0, [x2, #3, MUL VL]\n"
            "st1w z4.s, p0, [x2, #4, MUL VL]\n"
            "st1w z5.s, p0, [x2, #5, MUL VL]\n"
            "st1w z6.s, p0, [x2, #6, MUL VL]\n"
            "st1w z7.s, p0, [x2, #7, MUL VL]\n"
            "st1w z8.s, p0, [x2, #-8, MUL VL]\n"
            "st1w z9.s, p0, [x2, #-7, MUL VL]\n"
            "st1w z10.s, p0, [x2, #-6, MUL VL]\n"
            "st1w z11.s, p0, [x2, #-5, MUL VL]\n"
            "st1w z12.s, p0, [x2, #-4, MUL VL]\n"
            "st1w z13.s, p0, [x2, #-3, MUL VL]\n"
            "st1w z14.s, p0, [x2, #-2, MUL VL]\n"
            "st1w z15.s, p0, [x2, #-1, MUL VL]\n"
            "st1w z16.s, p0, [x2]\n"
            "st1w z17.s, p0, [x2, #1, MUL VL]\n"
            "st1w z18.s, p0, [x2, #2, MUL VL]\n"
            "st1w z19.s, p0, [x2, #3, MUL VL]\n"
            "st1w z20.s, p0, [x2, #4, MUL VL]\n"
            "st1w z21.s, p0, [x2, #5, MUL VL]\n"
            "st1w z22.s, p0, [x2, #6, MUL VL]\n"
            "st1w z23.s, p0, [x2, #7, MUL VL]\n"
            "st1w z24.s, p0, [x2, #-8, MUL VL]\n"
            "st1w z25.s, p0, [x2, #-7, MUL VL]\n"
            "st1w z26.s, p0, [x2, #-6, MUL VL]\n"
            "st1w z27.s, p0, [x2, #-5, MUL VL]\n"
            "st1w z28.s, p0, [x2, #-4, MUL VL]\n"
            "st1w z29.s, p0, [x2, #-3, MUL VL]\n"
            "st1w z30.s, p0, [x2, #-2, MUL VL]\n"
            "st1w z31.s, p0, [x2, #-1, MUL VL]\n"
            "subs x1, x1, #1\n"
            "b.ne 1b\n"
            : 
            : [count] "r" (iter_count), [ptr] "r" (data)
            : "x1", "x2", "p0", "z0", "z1", "z2", "z3", "z4", "z5", 
              "z6", "z7", "z8", "z9", "z10", "z11", "z12", 
              "z13", "z14", "z15", "z16", "z17", "z18", "z19",
              "z20", "z21", "z22", "z23", "z24", "z25", "z26",
              "z27", "z28", "z29", "z30", "z31", "memory", "cc"
        );
        
        asm volatile("isb" ::: "memory");
        uint64_t end = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        double elapsed_ns = (double)(end - start - loop_overhead);
        results[run] = elapsed_ns / (double)ITERATIONS;
    }
    
    stats_t s = compute_stats(results, NUM_RUNS);
    printf("Latency (throughput): %.3f ns (mean), %.3f ns (stddev)\n", s.mean, s.stddev);
    printf("Throughput: %.2f M ops/sec\n", 1000.0 / s.mean);
    
    free(data);
}

static void test_fmla_throughput(void) {
    printf("\n=== Testing FMLA (SVE) Throughput ===\n");
    
    uint64_t loop_overhead = g_loop_overhead;
    double results[NUM_RUNS];
    
    for (int run = 0; run < NUM_RUNS; run++) {
        uint64_t iter_count = ITERATIONS / UNROLL_FACTOR;
        
        asm volatile("isb" ::: "memory");
        uint64_t start = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        asm volatile (
            "mov x0, %[count]\n"
            "ptrue p0.s\n"
            "fmov z0.s, #1.0\n"
            "fmov z1.s, #2.0\n"
            "fmov z2.s, #3.0\n"
            "fmov z3.s, #4.0\n"
            "fmov z4.s, #5.0\n"
            "fmov z5.s, #6.0\n"
            "fmov z6.s, #7.0\n"
            "fmov z7.s, #8.0\n"
            "fmov z8.s, #9.0\n"
            "fmov z9.s, #10.0\n"
            "fmov z10.s, #11.0\n"
            "fmov z11.s, #12.0\n"
            "fmov z12.s, #13.0\n"
            "fmov z13.s, #14.0\n"
            "fmov z14.s, #15.0\n"
            "fmov z15.s, #16.0\n"
            "fmov z16.s, #17.0\n"
            "fmov z17.s, #18.0\n"
            "fmov z18.s, #19.0\n"
            "fmov z19.s, #20.0\n"
            "fmov z20.s, #21.0\n"
            "fmov z21.s, #22.0\n"
            "fmov z22.s, #23.0\n"
            "fmov z23.s, #24.0\n"
            "fmov z24.s, #25.0\n"
            "fmov z25.s, #26.0\n"
            "fmov z26.s, #27.0\n"
            "fmov z27.s, #28.0\n"
            "fmov z28.s, #29.0\n"
            "fmov z29.s, #30.0\n"
            "fmov z30.s, #31.0\n"
            "fmov z31.s, #31.0\n"
            "1:\n"
            "fmla z0.s, p0/m, z1.s, z2.s\n"
            "fmla z3.s, p0/m, z4.s, z5.s\n"
            "fmla z6.s, p0/m, z7.s, z8.s\n"
            "fmla z9.s, p0/m, z10.s, z11.s\n"
            "fmla z12.s, p0/m, z13.s, z14.s\n"
            "fmla z15.s, p0/m, z16.s, z17.s\n"
            "fmla z18.s, p0/m, z19.s, z20.s\n"
            "fmla z21.s, p0/m, z22.s, z23.s\n"
            "fmla z24.s, p0/m, z25.s, z26.s\n"
            "fmla z27.s, p0/m, z28.s, z29.s\n"
            "fmla z30.s, p0/m, z31.s, z1.s\n"
            "fmla z2.s, p0/m, z3.s, z4.s\n"
            "fmla z5.s, p0/m, z6.s, z7.s\n"
            "fmla z8.s, p0/m, z9.s, z10.s\n"
            "fmla z11.s, p0/m, z12.s, z13.s\n"
            "fmla z14.s, p0/m, z15.s, z16.s\n"
            "fmla z17.s, p0/m, z18.s, z19.s\n"
            "fmla z20.s, p0/m, z21.s, z22.s\n"
            "fmla z23.s, p0/m, z24.s, z25.s\n"
            "fmla z26.s, p0/m, z27.s, z28.s\n"
            "fmla z29.s, p0/m, z30.s, z31.s\n"
            "fmla z1.s, p0/m, z2.s, z3.s\n"
            "fmla z4.s, p0/m, z5.s, z6.s\n"
            "fmla z7.s, p0/m, z8.s, z9.s\n"
            "fmla z10.s, p0/m, z11.s, z12.s\n"
            "fmla z13.s, p0/m, z14.s, z15.s\n"
            "fmla z16.s, p0/m, z17.s, z18.s\n"
            "fmla z19.s, p0/m, z20.s, z21.s\n"
            "fmla z22.s, p0/m, z23.s, z24.s\n"
            "fmla z25.s, p0/m, z26.s, z27.s\n"
            "fmla z28.s, p0/m, z29.s, z30.s\n"
            "fmla z31.s, p0/m, z1.s, z2.s\n"
            "subs x0, x0, #1\n"
            "b.ne 1b\n"
            : 
            : [count] "r" (iter_count)
            : "x0", "p0", "z0", "z1", "z2", "z3", "z4", "z5", "z6", "z7",
              "z8", "z9", "z10", "z11", "z12", "z13", "z14", "z15", "z16",
              "z17", "z18", "z19", "z20", "z21", "z22", "z23", "z24", "z25",
              "z26", "z27", "z28", "z29", "z30", "z31", "cc"
        );
        
        asm volatile("isb" ::: "memory");
        uint64_t end = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        double elapsed_ns = (double)(end - start - loop_overhead);
        results[run] = elapsed_ns / (double)ITERATIONS;
    }
    
    stats_t s = compute_stats(results, NUM_RUNS);
    printf("Latency (throughput): %.3f ns (mean), %.3f ns (stddev)\n", s.mean, s.stddev);
    printf("Throughput: %.2f M ops/sec\n", 1000.0 / s.mean);
}

static void test_fmla_latency(void) {
    printf("\n=== Testing FMLA (SVE) Latency (Dependency Chain) ===\n");
    
    uint64_t loop_overhead = g_loop_overhead;
    double results[NUM_RUNS];
    
    for (int run = 0; run < NUM_RUNS; run++) {
        uint64_t iter_count = ITERATIONS / UNROLL_FACTOR;
        
        asm volatile("isb" ::: "memory");
        uint64_t start = get_time_ns();
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
            "fmla z0.s, p0/m, z1.s, z2.s\n"
            "subs x0, x0, #1\n"
            "b.ne 1b\n"
            : 
            : [count] "r" (iter_count)
            : "x0", "p0", "z0", "z1", "z2", "cc"
        );
        
        asm volatile("isb" ::: "memory");
        uint64_t end = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        double elapsed_ns = (double)(end - start - loop_overhead);
        results[run] = elapsed_ns / (double)ITERATIONS;
    }
    
    stats_t s = compute_stats(results, NUM_RUNS);
    printf("Latency (dependency chain): %.3f ns (mean), %.3f ns (stddev)\n", s.mean, s.stddev);
    printf("Throughput: %.2f M ops/sec\n", 1000.0 / s.mean);
}

static void test_fcmla_throughput(void) {
    printf("\n=== Testing FCMLA (SVE) Throughput ===\n");
    
    uint64_t loop_overhead = g_loop_overhead;
    double results[NUM_RUNS];
    
    for (int run = 0; run < NUM_RUNS; run++) {
        uint64_t iter_count = ITERATIONS / UNROLL_FACTOR;
        
        asm volatile("isb" ::: "memory");
        uint64_t start = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        asm volatile (
            "mov x0, %[count]\n"
            "ptrue p0.s\n"
            "fmov z0.s, #1.0\n"
            "fmov z1.s, #2.0\n"
            "fmov z2.s, #3.0\n"
            "fmov z3.s, #4.0\n"
            "fmov z4.s, #5.0\n"
            "fmov z5.s, #6.0\n"
            "fmov z6.s, #7.0\n"
            "fmov z7.s, #8.0\n"
            "fmov z8.s, #9.0\n"
            "fmov z9.s, #10.0\n"
            "fmov z10.s, #11.0\n"
            "fmov z11.s, #12.0\n"
            "fmov z12.s, #13.0\n"
            "fmov z13.s, #14.0\n"
            "fmov z14.s, #15.0\n"
            "fmov z15.s, #16.0\n"
            "fmov z16.s, #17.0\n"
            "fmov z17.s, #18.0\n"
            "fmov z18.s, #19.0\n"
            "fmov z19.s, #20.0\n"
            "fmov z20.s, #21.0\n"
            "fmov z21.s, #22.0\n"
            "fmov z22.s, #23.0\n"
            "fmov z23.s, #24.0\n"
            "fmov z24.s, #25.0\n"
            "fmov z25.s, #26.0\n"
            "fmov z26.s, #27.0\n"
            "fmov z27.s, #28.0\n"
            "fmov z28.s, #29.0\n"
            "fmov z29.s, #30.0\n"
            "fmov z30.s, #31.0\n"
            "fmov z31.s, #31.0\n"
            "1:\n"
            "fcmla z0.s, p0/m, z2.s, z3.s, #0\n"
            "fcmla z4.s, p0/m, z5.s, z6.s, #0\n"
            "fcmla z7.s, p0/m, z8.s, z9.s, #0\n"
            "fcmla z10.s, p0/m, z11.s, z12.s, #0\n"
            "fcmla z13.s, p0/m, z14.s, z15.s, #0\n"
            "fcmla z16.s, p0/m, z17.s, z18.s, #0\n"
            "fcmla z19.s, p0/m, z20.s, z21.s, #0\n"
            "fcmla z22.s, p0/m, z23.s, z24.s, #0\n"
            "fcmla z25.s, p0/m, z26.s, z27.s, #0\n"
            "fcmla z28.s, p0/m, z29.s, z30.s, #0\n"
            "fcmla z31.s, p0/m, z1.s, z2.s, #0\n"
            "fcmla z3.s, p0/m, z4.s, z5.s, #0\n"
            "fcmla z6.s, p0/m, z7.s, z8.s, #0\n"
            "fcmla z9.s, p0/m, z10.s, z11.s, #0\n"
            "fcmla z12.s, p0/m, z13.s, z14.s, #0\n"
            "fcmla z15.s, p0/m, z16.s, z17.s, #0\n"
            "fcmla z18.s, p0/m, z19.s, z20.s, #0\n"
            "fcmla z21.s, p0/m, z22.s, z23.s, #0\n"
            "fcmla z24.s, p0/m, z25.s, z26.s, #0\n"
            "fcmla z27.s, p0/m, z28.s, z29.s, #0\n"
            "fcmla z30.s, p0/m, z31.s, z1.s, #0\n"
            "fcmla z2.s, p0/m, z3.s, z4.s, #0\n"
            "fcmla z5.s, p0/m, z6.s, z7.s, #0\n"
            "fcmla z8.s, p0/m, z9.s, z10.s, #0\n"
            "fcmla z11.s, p0/m, z12.s, z13.s, #0\n"
            "fcmla z14.s, p0/m, z15.s, z16.s, #0\n"
            "fcmla z17.s, p0/m, z18.s, z19.s, #0\n"
            "fcmla z20.s, p0/m, z21.s, z22.s, #0\n"
            "fcmla z23.s, p0/m, z24.s, z25.s, #0\n"
            "fcmla z26.s, p0/m, z27.s, z28.s, #0\n"
            "fcmla z29.s, p0/m, z30.s, z31.s, #0\n"
            "fcmla z1.s, p0/m, z2.s, z3.s, #0\n"
            "subs x0, x0, #1\n"
            "b.ne 1b\n"
            : 
            : [count] "r" (iter_count)
            : "x0", "p0", "z0", "z1", "z2", "z3", "z4", "z5", "z6", "z7",
              "z8", "z9", "z10", "z11", "z12", "z13", "z14", "z15", "z16",
              "z17", "z18", "z19", "z20", "z21", "z22", "z23", "z24", "z25",
              "z26", "z27", "z28", "z29", "z30", "z31", "cc"
        );
        
        asm volatile("isb" ::: "memory");
        uint64_t end = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        double elapsed_ns = (double)(end - start - loop_overhead);
        results[run] = elapsed_ns / (double)ITERATIONS;
    }
    
    stats_t s = compute_stats(results, NUM_RUNS);
    printf("Latency (throughput): %.3f ns (mean), %.3f ns (stddev)\n", s.mean, s.stddev);
    printf("Throughput: %.2f M ops/sec\n", 1000.0 / s.mean);
}

static void test_fcmla_latency(void) {
    printf("\n=== Testing FCMLA (SVE) Latency (Dependency Chain) ===\n");
    
    uint64_t loop_overhead = g_loop_overhead;
    double results[NUM_RUNS];
    
    for (int run = 0; run < NUM_RUNS; run++) {
        uint64_t iter_count = ITERATIONS / UNROLL_FACTOR;
        
        asm volatile("isb" ::: "memory");
        uint64_t start = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        asm volatile (
            "mov x0, %[count]\n"
            "ptrue p0.s\n"
            "fmov z0.s, #1.0\n"
            "fmov z1.s, #2.0\n"
            "1:\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "fcmla z0.s, p0/m, z0.s, z1.s, #0\n"
            "subs x0, x0, #1\n"
            "b.ne 1b\n"
            : 
            : [count] "r" (iter_count)
            : "x0", "p0", "z0", "z1", "cc"
        );
        
        asm volatile("isb" ::: "memory");
        uint64_t end = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        double elapsed_ns = (double)(end - start - loop_overhead);
        results[run] = elapsed_ns / (double)ITERATIONS;
    }
    
    stats_t s = compute_stats(results, NUM_RUNS);
    printf("Latency (dependency chain): %.3f ns (mean), %.3f ns (stddev)\n", s.mean, s.stddev);
    printf("Throughput: %.2f M ops/sec\n", 1000.0 / s.mean);
}

static void test_prfm_pldl1keep(uint64_t *data, uint64_t iter_count) {
    asm volatile (
        "mov x1, %[count]\n"
        "1:\n"
        "prfm pldl1keep, [%[ptr]]\n"
        "prfm pldl1keep, [%[ptr], #64]\n"
        "prfm pldl1keep, [%[ptr], #128]\n"
        "prfm pldl1keep, [%[ptr], #192]\n"
        "prfm pldl1keep, [%[ptr], #256]\n"
        "prfm pldl1keep, [%[ptr], #320]\n"
        "prfm pldl1keep, [%[ptr], #384]\n"
        "prfm pldl1keep, [%[ptr], #448]\n"
        "prfm pldl1keep, [%[ptr], #512]\n"
        "prfm pldl1keep, [%[ptr], #576]\n"
        "prfm pldl1keep, [%[ptr], #640]\n"
        "prfm pldl1keep, [%[ptr], #704]\n"
        "prfm pldl1keep, [%[ptr], #768]\n"
        "prfm pldl1keep, [%[ptr], #832]\n"
        "prfm pldl1keep, [%[ptr], #896]\n"
        "prfm pldl1keep, [%[ptr], #960]\n"
        "prfm pldl1keep, [%[ptr], #1024]\n"
        "prfm pldl1keep, [%[ptr], #1088]\n"
        "prfm pldl1keep, [%[ptr], #1152]\n"
        "prfm pldl1keep, [%[ptr], #1216]\n"
        "prfm pldl1keep, [%[ptr], #1280]\n"
        "prfm pldl1keep, [%[ptr], #1344]\n"
        "prfm pldl1keep, [%[ptr], #1408]\n"
        "prfm pldl1keep, [%[ptr], #1472]\n"
        "prfm pldl1keep, [%[ptr], #1536]\n"
        "prfm pldl1keep, [%[ptr], #1600]\n"
        "prfm pldl1keep, [%[ptr], #1664]\n"
        "prfm pldl1keep, [%[ptr], #1728]\n"
        "prfm pldl1keep, [%[ptr], #1792]\n"
        "prfm pldl1keep, [%[ptr], #1856]\n"
        "prfm pldl1keep, [%[ptr], #1920]\n"
        "prfm pldl1keep, [%[ptr], #1984]\n"
        "subs x1, x1, #1\n"
        "b.ne 1b\n"
        : [ptr] "+r" (data)
        : [count] "r" (iter_count)
        : "x1", "memory", "cc"
    );
}

static void test_prfm_pldl1strm(uint64_t *data, uint64_t iter_count) {
    asm volatile (
        "mov x1, %[count]\n"
        "1:\n"
        "prfm pldl1strm, [%[ptr]]\n"
        "prfm pldl1strm, [%[ptr], #64]\n"
        "prfm pldl1strm, [%[ptr], #128]\n"
        "prfm pldl1strm, [%[ptr], #192]\n"
        "prfm pldl1strm, [%[ptr], #256]\n"
        "prfm pldl1strm, [%[ptr], #320]\n"
        "prfm pldl1strm, [%[ptr], #384]\n"
        "prfm pldl1strm, [%[ptr], #448]\n"
        "prfm pldl1strm, [%[ptr], #512]\n"
        "prfm pldl1strm, [%[ptr], #576]\n"
        "prfm pldl1strm, [%[ptr], #640]\n"
        "prfm pldl1strm, [%[ptr], #704]\n"
        "prfm pldl1strm, [%[ptr], #768]\n"
        "prfm pldl1strm, [%[ptr], #832]\n"
        "prfm pldl1strm, [%[ptr], #896]\n"
        "prfm pldl1strm, [%[ptr], #960]\n"
        "prfm pldl1strm, [%[ptr], #1024]\n"
        "prfm pldl1strm, [%[ptr], #1088]\n"
        "prfm pldl1strm, [%[ptr], #1152]\n"
        "prfm pldl1strm, [%[ptr], #1216]\n"
        "prfm pldl1strm, [%[ptr], #1280]\n"
        "prfm pldl1strm, [%[ptr], #1344]\n"
        "prfm pldl1strm, [%[ptr], #1408]\n"
        "prfm pldl1strm, [%[ptr], #1472]\n"
        "prfm pldl1strm, [%[ptr], #1536]\n"
        "prfm pldl1strm, [%[ptr], #1600]\n"
        "prfm pldl1strm, [%[ptr], #1664]\n"
        "prfm pldl1strm, [%[ptr], #1728]\n"
        "prfm pldl1strm, [%[ptr], #1792]\n"
        "prfm pldl1strm, [%[ptr], #1856]\n"
        "prfm pldl1strm, [%[ptr], #1920]\n"
        "prfm pldl1strm, [%[ptr], #1984]\n"
        "subs x1, x1, #1\n"
        "b.ne 1b\n"
        : [ptr] "+r" (data)
        : [count] "r" (iter_count)
        : "x1", "memory", "cc"
    );
}

static void test_prfm_pldl2keep(uint64_t *data, uint64_t iter_count) {
    asm volatile (
        "mov x1, %[count]\n"
        "1:\n"
        "prfm pldl2keep, [%[ptr]]\n"
        "prfm pldl2keep, [%[ptr], #64]\n"
        "prfm pldl2keep, [%[ptr], #128]\n"
        "prfm pldl2keep, [%[ptr], #192]\n"
        "prfm pldl2keep, [%[ptr], #256]\n"
        "prfm pldl2keep, [%[ptr], #320]\n"
        "prfm pldl2keep, [%[ptr], #384]\n"
        "prfm pldl2keep, [%[ptr], #448]\n"
        "prfm pldl2keep, [%[ptr], #512]\n"
        "prfm pldl2keep, [%[ptr], #576]\n"
        "prfm pldl2keep, [%[ptr], #640]\n"
        "prfm pldl2keep, [%[ptr], #704]\n"
        "prfm pldl2keep, [%[ptr], #768]\n"
        "prfm pldl2keep, [%[ptr], #832]\n"
        "prfm pldl2keep, [%[ptr], #896]\n"
        "prfm pldl2keep, [%[ptr], #960]\n"
        "prfm pldl2keep, [%[ptr], #1024]\n"
        "prfm pldl2keep, [%[ptr], #1088]\n"
        "prfm pldl2keep, [%[ptr], #1152]\n"
        "prfm pldl2keep, [%[ptr], #1216]\n"
        "prfm pldl2keep, [%[ptr], #1280]\n"
        "prfm pldl2keep, [%[ptr], #1344]\n"
        "prfm pldl2keep, [%[ptr], #1408]\n"
        "prfm pldl2keep, [%[ptr], #1472]\n"
        "prfm pldl2keep, [%[ptr], #1536]\n"
        "prfm pldl2keep, [%[ptr], #1600]\n"
        "prfm pldl2keep, [%[ptr], #1664]\n"
        "prfm pldl2keep, [%[ptr], #1728]\n"
        "prfm pldl2keep, [%[ptr], #1792]\n"
        "prfm pldl2keep, [%[ptr], #1856]\n"
        "prfm pldl2keep, [%[ptr], #1920]\n"
        "prfm pldl2keep, [%[ptr], #1984]\n"
        "subs x1, x1, #1\n"
        "b.ne 1b\n"
        : [ptr] "+r" (data)
        : [count] "r" (iter_count)
        : "x1", "memory", "cc"
    );
}

static void test_prfm_pldl2strm(uint64_t *data, uint64_t iter_count) {
    asm volatile (
        "mov x1, %[count]\n"
        "1:\n"
        "prfm pldl2strm, [%[ptr]]\n"
        "prfm pldl2strm, [%[ptr], #64]\n"
        "prfm pldl2strm, [%[ptr], #128]\n"
        "prfm pldl2strm, [%[ptr], #192]\n"
        "prfm pldl2strm, [%[ptr], #256]\n"
        "prfm pldl2strm, [%[ptr], #320]\n"
        "prfm pldl2strm, [%[ptr], #384]\n"
        "prfm pldl2strm, [%[ptr], #448]\n"
        "prfm pldl2strm, [%[ptr], #512]\n"
        "prfm pldl2strm, [%[ptr], #576]\n"
        "prfm pldl2strm, [%[ptr], #640]\n"
        "prfm pldl2strm, [%[ptr], #704]\n"
        "prfm pldl2strm, [%[ptr], #768]\n"
        "prfm pldl2strm, [%[ptr], #832]\n"
        "prfm pldl2strm, [%[ptr], #896]\n"
        "prfm pldl2strm, [%[ptr], #960]\n"
        "prfm pldl2strm, [%[ptr], #1024]\n"
        "prfm pldl2strm, [%[ptr], #1088]\n"
        "prfm pldl2strm, [%[ptr], #1152]\n"
        "prfm pldl2strm, [%[ptr], #1216]\n"
        "prfm pldl2strm, [%[ptr], #1280]\n"
        "prfm pldl2strm, [%[ptr], #1344]\n"
        "prfm pldl2strm, [%[ptr], #1408]\n"
        "prfm pldl2strm, [%[ptr], #1472]\n"
        "prfm pldl2strm, [%[ptr], #1536]\n"
        "prfm pldl2strm, [%[ptr], #1600]\n"
        "prfm pldl2strm, [%[ptr], #1664]\n"
        "prfm pldl2strm, [%[ptr], #1728]\n"
        "prfm pldl2strm, [%[ptr], #1792]\n"
        "prfm pldl2strm, [%[ptr], #1856]\n"
        "prfm pldl2strm, [%[ptr], #1920]\n"
        "prfm pldl2strm, [%[ptr], #1984]\n"
        "subs x1, x1, #1\n"
        "b.ne 1b\n"
        : [ptr] "+r" (data)
        : [count] "r" (iter_count)
        : "x1", "memory", "cc"
    );
}

static void test_prfm_pldl3keep(uint64_t *data, uint64_t iter_count) {
    asm volatile (
        "mov x1, %[count]\n"
        "1:\n"
        "prfm pldl3keep, [%[ptr]]\n"
        "prfm pldl3keep, [%[ptr], #64]\n"
        "prfm pldl3keep, [%[ptr], #128]\n"
        "prfm pldl3keep, [%[ptr], #192]\n"
        "prfm pldl3keep, [%[ptr], #256]\n"
        "prfm pldl3keep, [%[ptr], #320]\n"
        "prfm pldl3keep, [%[ptr], #384]\n"
        "prfm pldl3keep, [%[ptr], #448]\n"
        "prfm pldl3keep, [%[ptr], #512]\n"
        "prfm pldl3keep, [%[ptr], #576]\n"
        "prfm pldl3keep, [%[ptr], #640]\n"
        "prfm pldl3keep, [%[ptr], #704]\n"
        "prfm pldl3keep, [%[ptr], #768]\n"
        "prfm pldl3keep, [%[ptr], #832]\n"
        "prfm pldl3keep, [%[ptr], #896]\n"
        "prfm pldl3keep, [%[ptr], #960]\n"
        "prfm pldl3keep, [%[ptr], #1024]\n"
        "prfm pldl3keep, [%[ptr], #1088]\n"
        "prfm pldl3keep, [%[ptr], #1152]\n"
        "prfm pldl3keep, [%[ptr], #1216]\n"
        "prfm pldl3keep, [%[ptr], #1280]\n"
        "prfm pldl3keep, [%[ptr], #1344]\n"
        "prfm pldl3keep, [%[ptr], #1408]\n"
        "prfm pldl3keep, [%[ptr], #1472]\n"
        "prfm pldl3keep, [%[ptr], #1536]\n"
        "prfm pldl3keep, [%[ptr], #1600]\n"
        "prfm pldl3keep, [%[ptr], #1664]\n"
        "prfm pldl3keep, [%[ptr], #1728]\n"
        "prfm pldl3keep, [%[ptr], #1792]\n"
        "prfm pldl3keep, [%[ptr], #1856]\n"
        "prfm pldl3keep, [%[ptr], #1920]\n"
        "prfm pldl3keep, [%[ptr], #1984]\n"
        "subs x1, x1, #1\n"
        "b.ne 1b\n"
        : [ptr] "+r" (data)
        : [count] "r" (iter_count)
        : "x1", "memory", "cc"
    );
}

static void test_prfm_pldl3strm(uint64_t *data, uint64_t iter_count) {
    asm volatile (
        "mov x1, %[count]\n"
        "1:\n"
        "prfm pldl3strm, [%[ptr]]\n"
        "prfm pldl3strm, [%[ptr], #64]\n"
        "prfm pldl3strm, [%[ptr], #128]\n"
        "prfm pldl3strm, [%[ptr], #192]\n"
        "prfm pldl3strm, [%[ptr], #256]\n"
        "prfm pldl3strm, [%[ptr], #320]\n"
        "prfm pldl3strm, [%[ptr], #384]\n"
        "prfm pldl3strm, [%[ptr], #448]\n"
        "prfm pldl3strm, [%[ptr], #512]\n"
        "prfm pldl3strm, [%[ptr], #576]\n"
        "prfm pldl3strm, [%[ptr], #640]\n"
        "prfm pldl3strm, [%[ptr], #704]\n"
        "prfm pldl3strm, [%[ptr], #768]\n"
        "prfm pldl3strm, [%[ptr], #832]\n"
        "prfm pldl3strm, [%[ptr], #896]\n"
        "prfm pldl3strm, [%[ptr], #960]\n"
        "prfm pldl3strm, [%[ptr], #1024]\n"
        "prfm pldl3strm, [%[ptr], #1088]\n"
        "prfm pldl3strm, [%[ptr], #1152]\n"
        "prfm pldl3strm, [%[ptr], #1216]\n"
        "prfm pldl3strm, [%[ptr], #1280]\n"
        "prfm pldl3strm, [%[ptr], #1344]\n"
        "prfm pldl3strm, [%[ptr], #1408]\n"
        "prfm pldl3strm, [%[ptr], #1472]\n"
        "prfm pldl3strm, [%[ptr], #1536]\n"
        "prfm pldl3strm, [%[ptr], #1600]\n"
        "prfm pldl3strm, [%[ptr], #1664]\n"
        "prfm pldl3strm, [%[ptr], #1728]\n"
        "prfm pldl3strm, [%[ptr], #1792]\n"
        "prfm pldl3strm, [%[ptr], #1856]\n"
        "prfm pldl3strm, [%[ptr], #1920]\n"
        "prfm pldl3strm, [%[ptr], #1984]\n"
        "subs x1, x1, #1\n"
        "b.ne 1b\n"
        : [ptr] "+r" (data)
        : [count] "r" (iter_count)
        : "x1", "memory", "cc"
    );
}

static void test_prfm_latency(void) {
    printf("\n=== Testing PRFM Latency ===\n");
    
    uint64_t *data = (uint64_t *)aligned_alloc(64, ARRAY_SIZE * sizeof(uint64_t));
    if (!data) {
        perror("aligned_alloc failed");
        return;
    }
    for (int i = 0; i < ARRAY_SIZE; i++) {
        data[i] = i;
    }
    
    uint64_t loop_overhead = g_loop_overhead;
    double results[NUM_RUNS];
    
    for (int run = 0; run < NUM_RUNS; run++) {
        uint64_t iter_count = ITERATIONS / UNROLL_FACTOR;
        
        asm volatile("isb" ::: "memory");
        uint64_t start = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        asm volatile (
            "mov x1, %[count]\n"
            "1:\n"
            "prfm pldl1keep, [%[ptr]]\n"
            "prfm pldl1keep, [%[ptr], #64]\n"
            "prfm pldl1keep, [%[ptr], #128]\n"
            "prfm pldl1keep, [%[ptr], #192]\n"
            "prfm pldl1keep, [%[ptr], #256]\n"
            "prfm pldl1keep, [%[ptr], #320]\n"
            "prfm pldl1keep, [%[ptr], #384]\n"
            "prfm pldl1keep, [%[ptr], #448]\n"
            "prfm pldl1keep, [%[ptr], #512]\n"
            "prfm pldl1keep, [%[ptr], #576]\n"
            "prfm pldl1keep, [%[ptr], #640]\n"
            "prfm pldl1keep, [%[ptr], #704]\n"
            "prfm pldl1keep, [%[ptr], #768]\n"
            "prfm pldl1keep, [%[ptr], #832]\n"
            "prfm pldl1keep, [%[ptr], #896]\n"
            "prfm pldl1keep, [%[ptr], #960]\n"
            "prfm pldl1keep, [%[ptr], #1024]\n"
            "prfm pldl1keep, [%[ptr], #1088]\n"
            "prfm pldl1keep, [%[ptr], #1152]\n"
            "prfm pldl1keep, [%[ptr], #1216]\n"
            "prfm pldl1keep, [%[ptr], #1280]\n"
            "prfm pldl1keep, [%[ptr], #1344]\n"
            "prfm pldl1keep, [%[ptr], #1408]\n"
            "prfm pldl1keep, [%[ptr], #1472]\n"
            "prfm pldl1keep, [%[ptr], #1536]\n"
            "prfm pldl1keep, [%[ptr], #1600]\n"
            "prfm pldl1keep, [%[ptr], #1664]\n"
            "prfm pldl1keep, [%[ptr], #1728]\n"
            "prfm pldl1keep, [%[ptr], #1792]\n"
            "prfm pldl1keep, [%[ptr], #1856]\n"
            "prfm pldl1keep, [%[ptr], #1920]\n"
            "prfm pldl1keep, [%[ptr], #1984]\n"
            "subs x1, x1, #1\n"
            "b.ne 1b\n"
            : [ptr] "+r" (data)
            : [count] "r" (iter_count)
            : "x1", "memory", "cc"
        );
        
        asm volatile("isb" ::: "memory");
        uint64_t end = get_time_ns();
        asm volatile("isb" ::: "memory");
        
        double elapsed_ns = (double)(end - start - loop_overhead);
        results[run] = elapsed_ns / (double)ITERATIONS;
    }
    
    stats_t s = compute_stats(results, NUM_RUNS);
    printf("Issue time: %.3f ns (mean), %.3f ns (stddev)\n", s.mean, s.stddev);
    printf("Throughput: %.2f M ops/sec\n", 1000.0 / s.mean);
    
    free(data);
}

static void test_prfm_types(void) {
    printf("\n=== Testing PRFM Different Hints ===\n");
    
    uint64_t *data = (uint64_t *)aligned_alloc(64, ARRAY_SIZE * sizeof(uint64_t));
    if (!data) {
        perror("aligned_alloc failed");
        return;
    }
    uint64_t loop_overhead = g_loop_overhead;
    
    typedef void (*test_func_t)(uint64_t *data, uint64_t iter_count);
    
    const char *types[] = {"PLDL1KEEP", "PLDL1STRM", "PLDL2KEEP", "PLDL2STRM", "PLDL3KEEP", "PLDL3STRM"};
    
    test_func_t tests[] = {
        test_prfm_pldl1keep,
        test_prfm_pldl1strm,
        test_prfm_pldl2keep,
        test_prfm_pldl2strm,
        test_prfm_pldl3keep,
        test_prfm_pldl3strm
    };
    
    for (int t = 0; t < 6; t++) {
        double results[NUM_RUNS];
        
        for (int run = 0; run < NUM_RUNS; run++) {
            uint64_t iter_count = ITERATIONS / UNROLL_FACTOR;
            
            asm volatile("isb" ::: "memory");
            uint64_t start = get_time_ns();
            asm volatile("isb" ::: "memory");
            
            tests[t](data, iter_count);
            
            asm volatile("isb" ::: "memory");
            uint64_t end = get_time_ns();
            asm volatile("isb" ::: "memory");
            
            double elapsed_ns = (double)(end - start - loop_overhead);
            results[run] = elapsed_ns / (double)ITERATIONS;
        }
        
        stats_t s = compute_stats(results, NUM_RUNS);
        printf("%s: %.3f ns (mean), %.3f ns (stddev)\n", types[t], s.mean, s.stddev);
    }
    
    free(data);
}

int main(void) {
#ifndef __ARM_FEATURE_SVE
    printf("Error: SVE is not supported on this platform!\n");
    printf("Please compile with -march=armv8-a+sve flag\n");
    return 1;
#endif
    
    printf("AArch64 SVE Instruction Latency/Throughput Test\n");
    printf("=================================================\n");
#ifdef __ARM_FEATURE_SVE
    printf("SVE Vector Length: %lu bits (%lu bytes)\n", (unsigned long)svcntb() * 8, (unsigned long)svcntb());
#endif
    
    printf("\n=== Timer Configuration ===\n");
    init_timer_system();
    
    printf("\nSystem Counter Info:\n");
    printf("  cntfrq_el0: %lu MHz\n", g_cntfrq_hz / 1000000UL);
    printf("  Resolution: %lu ns per tick\n", g_timer_resolution_ns);
    printf("  Note: System counter runs at fixed freq, independent of CPU freq\n");
    
    printf("\nTest Parameters:\n");
    printf("  Iterations: %d (unroll factor: %d)\n", ITERATIONS, UNROLL_FACTOR);
    printf("  Runs: %d (statistics computed)\n", NUM_RUNS);
    printf("  Array Size: %d elements (%d KB)\n", ARRAY_SIZE, ARRAY_SIZE * 8 / 1024);
    
    printf("\nWarming up...\n");
    warmup_cache();
    measure_loop_overhead();
    printf("Warmup complete. Loop overhead: %.3f ns\n", (double)g_loop_overhead);
    
    test_ldr_throughput();
    test_ldr_latency();
    test_str_throughput();
    test_ld1w_throughput();
    test_st1w_throughput();
    test_fmla_throughput();
    test_fmla_latency();
    test_fcmla_throughput();
    test_fcmla_latency();
    test_prfm_latency();
    test_prfm_types();
    
    printf("\n=================================================\n");
    printf("Test completed!\n");
    
    return 0;
}