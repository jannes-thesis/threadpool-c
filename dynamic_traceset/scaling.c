//
// Created by jannes on 10/07/2020.
//

#include "scaling.h"
#include <stdbool.h>
#include <stddef.h>
#include <malloc.h>
#include <time.h>
#include <float.h>
#include <pthread.h>
#include "debug_macro.h"

/* ==================== INTERNAL ==================== */
static void metric_buf_insert_entry(metric_buffer* buffer, double scale_metric, double idle_metric, int amount_targets, unsigned long time);
static metric_datapoint metric_buf_get_entry(metric_buffer* buffer, int offset);
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
    if (pthread_spin_init(&adaptor->lock, PTHREAD_PROCESS_PRIVATE) != 0) {
        return NULL;
    }
    adaptor->params = *params;
    // create empty traceset
    adaptor->data.live_traceset = register_traceset(NULL, 0,
                                                    params->traced_syscalls, (int) params->amount_traced_syscalls);
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
    // free (includes deregister syscall) live traceset
    free_traceset(adaptor->data.live_traceset);
    // free snapshot (only some fields are allocated)
    free(adaptor->data.snapshot_traceset->data);
    free(adaptor->data.snapshot_traceset->sdata_arr);
    free(adaptor->data.snapshot_traceset);
    // free params
    free(adaptor->params.traced_syscalls);
}

bool ta_ready_for_update(trace_adaptor* adaptor) {
    return (adaptor->data.last_snapshot_ms + adaptor->params.interval_ms < current_time_ms());
}

bool ta_trylock(trace_adaptor* adaptor) {
    bool ret = pthread_spin_trylock(&adaptor->lock) == 0;
    if (ret) {
        debug_print("%s\n", "try lock succeed: yes");
    }
    else {
        debug_print("%s\n", "try lock succeed: no");
    }
    return ret;
}

void ta_unlock(trace_adaptor* adaptor) {
    pthread_spin_unlock(&adaptor->lock);
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
    if (register_traceset_target(adaptor->data.live_traceset->data->traceset_id, worker_pid)) {
        debug_print("registering %d as target successful\n", worker_pid);
    }
    else {
        debug_print("registering %d as target failed\n", worker_pid);
    }
}

void ta_remove_tracee(trace_adaptor* adaptor, pid_t worker_pid) {
    if (deregister_traceset_target(adaptor->data.live_traceset->data->traceset_id, worker_pid)) {
        debug_print("deregistering %d as target successful\n", worker_pid);
    }
    else {
        debug_print("deregistering %d as target failed\n", worker_pid);
    }
}


/* ==================== INTERNAL IMPL ==================== */

static void metric_buf_insert_entry(metric_buffer* buffer, double scale_metric, double idle_metric, int amount_targets, unsigned long time) {
    int new_entry_index = (buffer->index_newest + 1) % METRIC_BUFFER_SIZE ;
    buffer->ring[new_entry_index].scale_metric = scale_metric;
    buffer->ring[new_entry_index].idle_metric = idle_metric;
    buffer->ring[new_entry_index].amount_targets = amount_targets;
    buffer->ring[new_entry_index].time = time;
    buffer->index_newest = new_entry_index;
    if (buffer->size < METRIC_BUFFER_SIZE)
        buffer->size++;
}

static metric_datapoint metric_buf_get_entry(metric_buffer* buffer, int offset) {
    return buffer->ring[(buffer->index_newest + offset) % METRIC_BUFFER_SIZE];
}

static void init_traceset_interval(traceset_interval* interval, unsigned int amount_syscalls) {
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
            double scale_metric = adaptor->params.calc_scale_metric(&interval_data);
            double idle_metric = adaptor->params.calc_idle_metric(&interval_data);
            metric_buf_insert_entry(&adaptor->data.metric_history, scale_metric, idle_metric, amount_targets_snapshot,
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

/**
 * get advice on how (by what amounts) to scale the worker pool
 * @param adaptor
 * @return the difference of worker count: (#desired - #current)
 */
static int determine_scale_advice(trace_adaptor* adaptor) {
    metric_datapoint previous_data;
    metric_datapoint current_data;
    double scale_metric_diff;
    double scale_metric_diff_rel;

    if (adaptor->data.metric_history.size == 0) {
        return 0;
    }
    // always scale up initially
    else if (adaptor->data.metric_history.size == 1) {
        return (int) adaptor->params.step_size;
    }
    else if (adaptor->data.metric_history.size > 1) {
        previous_data = metric_buf_get_entry(&adaptor->data.metric_history, -1);
        current_data = metric_buf_get_entry(&adaptor->data.metric_history, 0);
        debug_print("previous metric: %f\n", previous_data.scale_metric);
        debug_print("current metric: %f\n", current_data.scale_metric);
        scale_metric_diff = current_data.scale_metric - previous_data.scale_metric;
        scale_metric_diff_rel = scale_metric_diff / previous_data.scale_metric;

        // if diff is very close to 0 (or exactly 0)
        if (scale_metric_diff > 100 * -DBL_MIN && scale_metric_diff < 100 * DBL_MIN) {
            return 0;
        }
        // scale_metric increased by at least 10%
        if (scale_metric_diff >= 0 && scale_metric_diff_rel >= 0.1) {
            return (int) adaptor->params.step_size;
        }
        // scale metric decreased by at least 10%
        else if (scale_metric_diff < 0 && scale_metric_diff_rel <= -0.1){
            return previous_data.amount_targets - current_data.amount_targets;
        }
        else {
            return 0;
        }
    }
}

