//
// Created by jannes on 10/07/2020.
//

#ifndef THREADPOOL_SCALING_H
#define THREADPOOL_SCALING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "lib_traceset/traceset.h"

#define METRIC_BUFFER_SIZE 10

typedef struct metric_datapoint {
    /* adaptor tries to maximize this */
    double scale_metric;
    /* adaptor resets pool when this drops to much lower than previously observed */
    double idle_metric;
    int amount_targets;
    unsigned long time;
} metric_datapoint;

typedef struct metric_buffer {
    metric_datapoint ring[METRIC_BUFFER_SIZE];
    size_t size;
    int index_newest;
} metric_buffer;

typedef struct traceset_interval {
    unsigned long start;
    unsigned long end;
    traceset interval_data;
} traceset_interval;

typedef struct trace_adaptor_params {
    /** algorithm parameters */
    unsigned long interval_ms;
    unsigned int step_size;
    /** user workload parameters */
    unsigned int amount_traced_syscalls;
    int* traced_syscalls;
    double (*calc_scale_metric) (traceset_interval* interval_data);
    double (*calc_idle_metric) (traceset_interval* interval_data);
} trace_adaptor_params;

typedef struct trace_adaptor_data {
    traceset* live_traceset;
    traceset* snapshot_traceset;
    unsigned long last_snapshot_ms;
    metric_buffer metric_history;
    double idle_metric_max;
} trace_adaptor_data;

typedef struct trace_adaptor {
    trace_adaptor_params params;
    trace_adaptor_data data;
    _Atomic(int) lock; // -1 is unlocked, lock with worker id
} trace_adaptor;

unsigned long current_time_ms(void);
trace_adaptor* ta_create(trace_adaptor_params* params);
void ta_destroy(trace_adaptor* adaptor);

bool ta_ready_for_update(trace_adaptor* adaptor);
bool ta_lock(trace_adaptor* adaptor, size_t worker_id);
void ta_unlock(trace_adaptor* adaptor);
int ta_get_scale_advice(trace_adaptor* adaptor);

void ta_add_tracee(trace_adaptor* adaptor, pid_t worker_pid);
void ta_remove_tracee(trace_adaptor* adaptor, pid_t worker_pid);

/**
 * API usage by workers:
 * 1. pool workers check if adaptor is ready for update
 * (both the interval has passed and no update is currently performing)
 * if ready, continue, otherwise skip update
 * 2. try to lock adaptor, if successful continue
 * 3. get scale advice (also does data update), otherwise stop
 * 4. unlock adaptor
 * 5. push appropriate scale commands
 */

#endif //THREADPOOL_SCALING_H
