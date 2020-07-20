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
#include "debug_macro.h"

/* ==================== INTERNAL ==================== */
static void metric_buf_insert_entry(metric_buffer* buffer, float value, int amount_targets, unsigned long time);
static scale_metric_datapoint* metric_buf_get_entry(metric_buffer* buffer, int offset);
static void copy_traceset(traceset* from, traceset* to);
static void diff_traceset(traceset* later, traceset* earlier, unsigned long earlier_time, traceset_interval* diff);
static bool update_snapshot(trace_adaptor* adaptor);
static int determine_scale_advice(trace_adaptor* adaptor);

/**
 * @param buffer: metric_buffer*
 * @param cursor: scale_metric_datapoint*
 * @param i: int*
 */
#define metric_buf_for_each_new_to_old(buffer, cursor, i) \
    for (*i = buffer->index_newest, cursor = buffer->ring[*i]; \
         *i != (*i + 1 + (METRIC_BUFFER_SIZE - buffer->size)) % METRIC_BUFFER_SIZE; \
         *i = (*i - 1) % METRIC_BUFFER_SIZE, cursor = buffer->ring[*i])  \

/* ==================== API ==================== */

unsigned long current_time_ms() {
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);

    unsigned long ms = (unsigned long) (spec.tv_nsec / 1.0e6) + (1000 * spec.tv_sec);
    return ms;
}

trace_adaptor* ta_create(trace_adaptor_params* params) {
    trace_adaptor* adaptor = malloc(sizeof(trace_adaptor));
    if (adaptor == NULL) {
        return NULL;
    }
    adaptor->lock = -1;
    adaptor->params = *params;
    // create empty traceset
    adaptor->data.live_traceset = register_traceset(NULL, 0,
                                                    params->traced_syscalls, params->amount_traced_syscalls);
    if (adaptor->data.live_traceset == NULL) {
        goto err;
    }
    // allocate traceset structure for snapshotting
    adaptor->data.snapshot_traceset = malloc(sizeof(traceset));
    if (adaptor->data.snapshot_traceset == NULL) {
        goto err;
    }
    adaptor->data.snapshot_traceset->data = malloc(sizeof(traceset_data));
    adaptor->data.snapshot_traceset->sdata_arr = malloc(sizeof(traceset_syscall_data) * params->amount_traced_syscalls);
    if (adaptor->data.snapshot_traceset->data == NULL || adaptor->data.snapshot_traceset->sdata_arr == NULL) {
        goto err;
    }
    // make first snapshot to initialize snapshot structure with zeroed data
    copy_traceset(adaptor->data.live_traceset, adaptor->data.snapshot_traceset);
    adaptor->data.last_snapshot_ms = current_time_ms();
    return adaptor;

    err:
    if (adaptor->data.snapshot_traceset != NULL) {
        free(adaptor->data.snapshot_traceset->data);
        free(adaptor->data.snapshot_traceset->sdata_arr);
    }
    free(adaptor->data.snapshot_traceset);
    deregister_traceset(adaptor->data.live_traceset->data->traceset_id);
    free(adaptor);
    return NULL;
}

void ta_destroy(trace_adaptor* adaptor) {
    // TODO
}

bool ta_ready_for_update(trace_adaptor* adaptor) {
    return (adaptor->data.last_snapshot_ms + adaptor->params.interval_ms < current_time_ms()) && (adaptor->lock == -1);
}

bool ta_lock(trace_adaptor* adaptor, size_t worker_id) {
    int unlocked = -1;
    return atomic_compare_exchange_weak(&adaptor->lock, &unlocked, worker_id);
}

void ta_unlock(trace_adaptor* adaptor) {
    adaptor->lock = -1;
}

int ta_get_scale_advice(trace_adaptor* adaptor) {
    unsigned long now_ms = current_time_ms();
    if (adaptor->data.last_snapshot_ms + adaptor->params.interval_ms < now_ms) {
        if (update_snapshot(adaptor)) {
            return determine_scale_advice(adaptor);
        }
    }
    return 0;
}

void ta_add_tracee(trace_adaptor* adaptor, pid_t worker_pid) {
    register_traceset_target(adaptor->data.live_traceset->data->traceset_id, worker_pid);
}

void ta_remove_tracee(trace_adaptor* adaptor, pid_t worker_pid) {
    deregister_traceset_target(adaptor->data.live_traceset->data->traceset_id, worker_pid);
}


/* ==================== INTERNAL IMPL ==================== */

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

static void init_traceset_interval(traceset_interval* interval, int amount_syscalls) {
    // TODO error handling
    interval->interval_data.data = malloc(sizeof(traceset_data));
    interval->interval_data.sdata_arr = malloc(amount_syscalls * sizeof(traceset_syscall_data));
}

static void free_traceset_interval_fields(traceset_interval* interval) {
    free(interval->interval_data.data);
    free(interval->interval_data.sdata_arr);
}

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
static bool update_snapshot(trace_adaptor* adaptor) {
    debug_print("%s\n", "attempt interval snapshot");
    traceset_interval interval_data;
    bool ret;
    init_traceset_interval(&interval_data, adaptor->params.amount_traced_syscalls);
    int amount_targets_snapshot = adaptor->data.snapshot_traceset->data->amount_targets;
    if (adaptor->data.live_traceset->data->amount_targets == amount_targets_snapshot) {
        debug_print("%s\n", "take difference of live and last snapshot");
        diff_traceset(adaptor->data.live_traceset, adaptor->data.snapshot_traceset, adaptor->data.last_snapshot_ms,
                      &interval_data);
        debug_print("%s\n", "copy live to snapshot");
        copy_traceset(adaptor->data.live_traceset, adaptor->data.snapshot_traceset);
        adaptor->data.last_snapshot_ms = current_time_ms();
        if (adaptor->data.snapshot_traceset->data->amount_targets == amount_targets_snapshot) {
            float scale_metric = adaptor->params.scaling_metric(&interval_data);
            metric_buf_insert_entry(&adaptor->data.scale_metric_history, scale_metric, amount_targets_snapshot,
                                    interval_data.end);
            debug_print("%s\n", "took valid interval snapshot");
            ret = true;
        } else {
            debug_print("%s\n", "taken snapshot invalid, amount of targets changed during snapshotting");
            ret = false;
        }
    } else {
        debug_print("%s\n", "can't take valid snapshot, amount of targets changed");
        copy_traceset(adaptor->data.live_traceset, adaptor->data.snapshot_traceset);
        ret = false;
    }
    free_traceset_interval_fields(&interval_data);
    return ret;
}

static int determine_scale_advice(trace_adaptor* adaptor) {
    // if current interval scale metric is worse than one before:
    //      adjust back to previous size OR if not utilized
    scale_metric_datapoint* current_interval_metric;
    scale_metric_datapoint* previous_interval_metric;
    if (adaptor->data.scale_metric_history.size > 1) {
        current_interval_metric = metric_buf_get_entry(&adaptor->data.scale_metric_history, 0);
        previous_interval_metric = metric_buf_get_entry(&adaptor->data.scale_metric_history, 1);
        if (previous_interval_metric->metric > current_interval_metric->metric) {
            return previous_interval_metric->amount_targets - current_interval_metric->amount_targets;
        }
        else {
            return current_interval_metric->amount_targets;
        }
    }
}

