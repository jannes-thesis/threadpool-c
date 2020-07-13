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

typedef struct scale_metric_datapoint {
    float metric;
    int amount_targets;
    unsigned long time;
} scale_metric_datapoint;

typedef struct metric_buffer {
    scale_metric_datapoint ring[METRIC_BUFFER_SIZE];
    size_t size;
    int index_newest;
} metric_buffer;

void metric_buf_insert_entry(metric_buffer* buffer, float value, int amount_targets, unsigned long time) {
    int new_entry_index = (buffer->index_newest + 1) % METRIC_BUFFER_SIZE ;
    buffer->ring[new_entry_index].metric = value;
    buffer->ring[new_entry_index].amount_targets = amount_targets;
    buffer->ring[new_entry_index].time = time;
    if (buffer->size < METRIC_BUFFER_SIZE)
        buffer->size++;
}

/**
 * @param buffer: metric_buffer*
 * @param cursor: scale_metric_datapoint*
 * @param i: int*
 */
#define metric_buf_for_each_new_to_old(buffer, cursor, i) \
    for (*i = buffer->index_newest, cursor = buffer->ring[*i]; \
         *i != (*i + 1 + (METRIC_BUFFER_SIZE - buffer->size)) % METRIC_BUFFER_SIZE; \
         *i = (*i - 1) % METRIC_BUFFER_SIZE, cursor = buffer->ring[*i])  \


typedef struct traceset_interval {
    unsigned long start;
    unsigned long end;
    traceset interval_data;
} traceset_interval;

typedef struct adaptor_parameters {
    unsigned long interval_ms;
    unsigned int amount_traced_syscalls;
    int* traced_syscalls;
    float (*scaling_metric) (traceset_interval* interval_data);
} adaptor_parameters;

typedef struct adaptor_data {
    traceset* live_traceset;
    traceset* snapshot_traceset;
    unsigned long last_snapshot_ms;
    metric_buffer* scale_metric_history;
} adaptor_data;

typedef struct traceset_adaptor {
    adaptor_parameters params;
    adaptor_data data;
    _Atomic(int) lock; // -1 is unlocked, lock with worker id
} traceset_adaptor;

/**
 * API usage by workers:
 * 1. pool workers check if adaptor is ready for update
 * (both the interval has passed and no update is currently peforming)
 * if ready, continue, otherwise skip update
 * 2. try to lock adaptor, if successful continue
 * 3. get scale advice (also does data update), otherwise stop
 * 4. unlock adaptor
 * 5. push appropriate scale commands
 */

bool ready_for_update(traceset_adaptor* adaptor);
bool lock_adaptor(traceset_adaptor* adaptor, size_t worker_id);
void unlock_adaptor(traceset_adaptor* adaptor);
int get_scale_advice(traceset_adaptor* adaptor);

#endif //THREADPOOL_SCALING_H
