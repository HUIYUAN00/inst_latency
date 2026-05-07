#include "stats.h"
#include <stdio.h>
#include <math.h>

stats_t compute_detailed_stats(double *values, int n) {
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

void print_result_with_precision(const char *test_name, stats_t s) {
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