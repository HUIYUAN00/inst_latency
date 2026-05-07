#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void init_timer_backend(void);
double get_time_ns(void);
double get_tick_to_ns_factor(void);
void calibrate_loop_overhead(int iterations, int num_runs);
double get_loop_overhead_ns(void);

#endif