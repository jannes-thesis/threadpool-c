//
// Created by jannes on 10/07/2020.
//

#include "scaling.h"
#include <stdbool.h>
#include <stddef.h>
#include <math.h>
#include <malloc.h>
#include <time.h>
#include <stdatomic.h>

static long current_time_ms() {
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);

    unsigned long ms = (unsigned long) (spec.tv_nsec / 1.0e6) + (1000 * spec.tv_sec);
    return ms;
}

static void metric_buf_insert_entry(metric_buffer* buffer, float value, int amount_targets, unsigned long time) {
    int new_entry_index = (buffer->index_newest + 1) % METRIC_BUFFER_SIZE ;
    buffer->ring[new_entry_index].metric = value;
    buffer->ring[new_entry_index].amount_targets = amount_targets;
    buffer->ring[new_entry_index].time = time;
    if (buffer->size < METRIC_BUFFER_SIZE)
        buffer->size++;
}

static scale_metric_datapoint* metric_buf_get_entry(metric_buffer* buffer, int offset) {
    return &buffer->ring[(buffer->index_newest + offset) % METRIC_BUFFER_SIZE];
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

static void copy_traceset(traceset* from, traceset* to) {
    to->data->amount_targets = from->data->amount_targets;
    to->data->write_bytes = from->data->write_bytes;
    to->data->read_bytes = from->data->read_bytes;
    for (int i = 0; i < from->amount_syscalls; i++) {
        to->sdata_arr[i].total_time = from->sdata_arr[i].total_time;
        to->sdata_arr[i].count = from->sdata_arr[i].count;
    }
}

static void diff_traceset(traceset* later, traceset* earlier, unsigned long earlier_time, traceset_interval* diff) {
    diff->interval_data.data->write_bytes = later->data->write_bytes - earlier->data->write_bytes;
    diff->interval_data.data->read_bytes = later->data->read_bytes - earlier->data->read_bytes;
    for (int i = 0; i < earlier->amount_syscalls; i++) {
        diff->interval_data.sdata_arr[i].total_time = later->sdata_arr[i].total_time - earlier->sdata_arr[i].total_time;
        diff->interval_data.sdata_arr[i].count = later->sdata_arr[i].count - earlier->sdata_arr[i].count;
    }
    diff->start = earlier_time;
    diff->end = current_time_ms();
}

/**
 * calculate metrics for interval from last snapshot to now & update snapshot
 * if no changes in amount targets, add new scale metric to scale buffer
 * @param adaptor
 * @return true if valid interval (amount of trace targets did not change)
 */
static bool take_snapshot(traceset_adaptor* adaptor) {
    traceset_interval interval_data;
    int amount_targets_snapshot = adaptor->data.snapshot_traceset->data->amount_targets;
    if (adaptor->data.live_traceset->data->amount_targets == amount_targets_snapshot) {
        diff_traceset(adaptor->data.live_traceset, adaptor->data.snapshot_traceset, adaptor->data.last_snapshot_ms,
                      &interval_data);
        copy_traceset(adaptor->data.live_traceset, adaptor->data.snapshot_traceset);
        if (adaptor->data.snapshot_traceset->data->amount_targets == amount_targets_snapshot) {
            float scale_metric = adaptor->params.scaling_metric(&interval_data);
            metric_buf_insert_entry(adaptor->data.scale_metric_history, scale_metric, amount_targets_snapshot,
                                    interval_data.end);
            return true;
        } else {
            return false;
        }
    } else {
        copy_traceset(adaptor->data.live_traceset, adaptor->data.snapshot_traceset);
        return false;
    }
}

static int determine_scale_advice(traceset_adaptor* adaptor) {
    // if current interval scale metric is worse than one before:
    //      adjust back to previous size OR if not utilized
    scale_metric_datapoint* current_interval_metric;
    scale_metric_datapoint* previous_interval_metric;
    if (adaptor->data.scale_metric_history->size > 1) {
        current_interval_metric = metric_buf_get_entry(adaptor->data.scale_metric_history, 0);
        previous_interval_metric = metric_buf_get_entry(adaptor->data.scale_metric_history, 1);
        if (previous_interval_metric->metric > current_interval_metric->metric) {
            return previous_interval_metric->amount_targets - current_interval_metric->amount_targets;
        }
        else {
            return current_interval_metric->amount_targets;
        }
    }
}

traceset_adaptor* create_adaptor(adaptor_parameters* params) {
    traceset_adaptor* adaptor = malloc(sizeof(traceset_adaptor));
    if (adaptor == NULL) {
        return NULL;
    }
    adaptor->lock = -1;
    adaptor->params = *params;
    adaptor->data.last_snapshot_ms = 0;
    adaptor->data.live_traceset = register_traceset(NULL, 0,
            params->traced_syscalls, params->amount_traced_syscalls);
    adaptor->data.snapshot_traceset = malloc(sizeof(traceset));
    adaptor->data.snapshot_traceset->data = malloc(sizeof(traceset_data));
    adaptor->data.snapshot_traceset->sdata_arr = malloc(sizeof(traceset_syscall_data) * params->amount_traced_syscalls);
    //TODO error handling
    return adaptor;
}

bool ready_for_update(traceset_adaptor* adaptor) {
    return (adaptor->data.last_snapshot_ms + adaptor->params.interval_ms < current_time_ms()) && (adaptor->lock == -1);
}

bool lock_adaptor(traceset_adaptor* adaptor, size_t worker_id) {
    int unlocked = -1;
    return atomic_compare_exchange_weak(&adaptor->lock, &unlocked, worker_id);
}

void unlock_adaptor(traceset_adaptor* adaptor) {
    adaptor->lock = -1;
}

int get_scale_advice(traceset_adaptor* adaptor) {
    unsigned long now_ms = current_time_ms();
    if (adaptor->data.last_snapshot_ms + adaptor->params.interval_ms < now_ms) {
        if (take_snapshot(adaptor)) {
            return determine_scale_advice(adaptor);
        }
    }
    return 0;
}

void add_tracee(pid_t worker_id) {

}

void remove_tracee(pid_t worker_id) {

}
