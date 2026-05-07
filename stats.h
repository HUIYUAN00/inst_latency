#ifndef STATS_H
#define STATS_H

typedef struct {
    double mean;
    double stddev;
    double min;
    double max;
    double sem;
    double error_pct;
} stats_t;

stats_t compute_detailed_stats(double *values, int n);
void print_result_with_precision(const char *test_name, stats_t s);

#endif