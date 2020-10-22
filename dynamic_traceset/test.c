//
// Created by jannes on 13/07/2020.
//

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>
#include <string.h>

#include "debug_macro.h"
#include "scaling.h"
#include "adaptive_tpool.h"

static const size_t NUM_THREADS = 1;
static const size_t NUM_ITEMS = 1000;

double calc_metric(traceset_interval* interval) {
    unsigned long interval_length = interval->end - interval->start;
    double result = (double) interval->interval_data.data->write_bytes / (double) interval_length;
    debug_print("start %lu\n", interval->start);
    debug_print("end %lu\n", interval->end);
    debug_print("bytes written / ms: %f\n", result);
    debug_print("total bytes written %llu\n", interval->interval_data.data->write_bytes);
    return result;
}

trace_adaptor* get_adaptor(int interval_ms) {
    trace_adaptor_params params;
    int write_syscall_nr = 1;
    params.amount_traced_syscalls = 1;
    params.traced_syscalls = &write_syscall_nr;
    params.interval_ms = interval_ms;
    params.calc_scale_metric = calc_metric;
    params.calc_idle_metric = calc_metric;
    params.step_size = 2;
    trace_adaptor* result = ta_create(&params);
    if (result == NULL) {
        debug_print("%s\n", "adaptor NULL");
    }
    return result;
}

void work_function(void *arg) {
    int* valp = arg;
    debug_print("%s %d\n", "user function start", *valp);
    char filename[50];
    sprintf(filename, "wout%d.txt", *valp);
    FILE* fp = fopen(filename, "w");
    int fd = fileno(fp);
    for (int i = 0; i < 1000; ++i) {
        fprintf(fp, "this is a line\n");
        fsync(fd);
    }
    debug_print("%s %d\n", "user function end", *valp);
}

int tpool_test() {
    int is[NUM_ITEMS];
    debug_print("%s\n", "creating tpool");
    trace_adaptor* adaptor = get_adaptor(2000);
    if (adaptor == NULL) {
        debug_print("%s\n", "could not create traceset adaptor for tpool");
        return -1;
    }
    threadpool tm = tpool_create_2(NUM_THREADS, adaptor);
    debug_print("%s\n", "start submitting jobs to tpool");
    for (int i = 0; i < NUM_ITEMS; i++) {
        is[i] = i;
        tpool_submit_job(tm, work_function, &is[i]);
    }
    debug_print("%s\n", "waiting for tpool");
    tpool_wait(tm);
    debug_print("%s\n", "destroying tpool");
    tpool_destroy(tm);
    return 0;
}

int tracing_test() {
    trace_adaptor* adaptor = get_adaptor(2000);
    pid_t self = getpid();
    debug_print("%s\n", "add self as tracee");
    ta_add_tracee(adaptor, self);
    for (int i = 0; i < 10; ++i) {
        debug_print("round %d: is ready for update? %d\n", i, ta_ready_for_update(adaptor));
        debug_print("round %d: get scale advice\n", i);
        int advice = ta_get_scale_advice(adaptor);
        debug_print("round %d: advice to scale by %d\n", i, advice);
        sleep(3);
    }
    ta_destroy(adaptor);
    return 0;
}

int peak_int(void* mem) {
    int* ptr = (int*) mem;
    return ptr[0];
}

long peak_long(void* mem) {
    long* ptr = (long*) mem;
    return ptr[0];
}

int traceset_test(pid_t tracee) {
    int* write_syscall = malloc(sizeof(int));
    *write_syscall = 1;
    traceset* tset = register_traceset(NULL, 0, write_syscall, 1);
    debug_print("%s\n", "add tracee");
    bool is_success = register_traceset_target(tset->data->traceset_id, tracee);
    if (is_success) {
        debug_print("%s\n", "success");
    } else {
        debug_print("%s\n", "failure");
    }
    traceset_data* tset_data = tset->data;
    traceset_syscall_data* tset_sysdata = tset->sdata_arr;
    deregister_traceset(tset->data->traceset_id);
    return 0;
}

/**
 * a test run where the items are submitted to the pool at increasing frequency
 * @param interval_ms
 * @param num_items
 */
int tpool_write_work_test(int interval_ms, int num_items) {
    int is[NUM_ITEMS];
    debug_print("%s\n", "creating tpool");
    trace_adaptor* adaptor = get_adaptor(interval_ms);
    if (adaptor == NULL) {
        debug_print("%s\n", "could create traceset adaptor for tpool");
        return -1;
    }
    threadpool tm = tpool_create_2(1, adaptor);
    debug_print("%s\n", "start submitting jobs to tpool");
    for (int i = 0; i < num_items; i++) {
        is[i] = i;
        tpool_submit_job(tm, work_function, &is[i]);
        if (i < num_items / 4)
            usleep(100000); // 100 ms
        else if (i < num_items / 2)
            usleep(30000); // 30ms
        else if (i < num_items / 4 * 3)
            usleep(10000); // 10ms
        else
            usleep(1000); // 1ms
    }
    debug_print("%s\n", "waiting for tpool");
    tpool_wait(tm);
    debug_print("%s\n", "destroying tpool");
    tpool_destroy(tm);
    debug_print("%s\n", "deleting worker output");
    char filename[50];
    for (int j = 0; j < num_items; j++) {
        sprintf(filename, "wout%d.txt", j);
        remove(filename);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("specify test name: tracing/tpool/tpool_incload\n");
        return -1;
    } else {
        if (strcmp(argv[1], "tracing") == 0) {
            debug_print("%s\n", "RUNNING TRACING.C TEST");
            debug_print("TRACING.C TEST RETURN: %d\n", tracing_test());
        }
        else if (strcmp(argv[1], "tpool") == 0) {
            debug_print("%s\n", "RUNNING SIMPLE TPOOL TEST");
            debug_print("TRACING.C TEST RETURN: %d\n", tpool_test());
        }
        else if (strcmp(argv[1], "tpool_incload") == 0) {
            debug_print("%s\n", "RUNNING SIMPLE TPOOL TEST");
            debug_print("TRACING.C TEST RETURN: %d\n", tpool_write_work_test(1000, 200));
        }
        else if (strcmp(argv[1], "traceset") == 0) {
            pid_t tracee = atoi(argv[2]);
            debug_print("%s\n", "RUNNING TRACESET WITH TARGET TEST");
            debug_print("TRACESET TEST RETURN: %d\n", traceset_test(tracee));
        }
    }
    return 0;
}





