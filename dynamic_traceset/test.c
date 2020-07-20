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

float calc_metric(traceset_interval* interval) {
    unsigned long interval_length = interval->start - interval->end;
    float result = (double) interval->interval_data.data->write_bytes / (double) interval_length;
    debug_print("bytes written / ms: %f\n", result);
    return result;
}

trace_adaptor* get_adaptor(int interval_ms) {
    trace_adaptor_params params;
    int write_syscall_nr = 1;
    params.amount_traced_syscalls = 1;
    params.traced_syscalls = &write_syscall_nr;
    params.interval_ms = interval_ms;
    params.scaling_metric = calc_metric;
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
    for (int i = 0; i < 1000; ++i) {
        fprintf(fp, "this is a line\n");
    }
    debug_print("%s %d\n", "user function end", *valp);
}

int tpool_test() {
    debug_print("%s\n", "creating tpool");
    trace_adaptor* adaptor = get_adaptor(2000);
    if (adaptor == NULL) {
        debug_print("%s\n", "could create traceset adaptor for tpool");
        return -1;
    }
    threadpool tm = tpool_create_2(NUM_THREADS, adaptor);
    debug_print("%s\n", "start submitting jobs to tpool");
    for (int i = 0; i < NUM_ITEMS; i++) {
        tpool_submit_job(tm, work_function, &i);
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

/**
 * a test run where the items are submitted to the pool at increasing frequency
 * @param interval_ms
 * @param num_items
 */
int tpool_write_work_test(int interval_ms, int num_items) {
    debug_print("%s\n", "creating tpool");
    trace_adaptor* adaptor = get_adaptor(interval_ms);
    if (adaptor == NULL) {
        debug_print("%s\n", "could create traceset adaptor for tpool");
        return -1;
    }
    threadpool tm = tpool_create_2(1, adaptor);
    debug_print("%s\n", "start submitting jobs to tpool");
    for (int i = 0; i < num_items; i++) {
        tpool_submit_job(tm, work_function, &i);
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
    for (int j = 0; j < num_items; ++j) {
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
            debug_print("TRACING.C TEST RETURN: %d\n", tpool_write_work_test(3000, 10000));
        }
    }
    return 0;
}





