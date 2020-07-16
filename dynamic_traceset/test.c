//
// Created by jannes on 13/07/2020.
//

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>

#include "debug_macro.h"
#include "scaling.h"
#include "adaptive_tpool.h"

static const size_t num_threads = 1;
static const size_t num_items = 1000;

float calc_metric(traceset_interval* interval) {
    unsigned long interval_length = interval->start - interval->end;
    float result = (double) interval->interval_data.data->write_bytes / (double) interval_length;
    debug_print("bytes written / ms: %f\n", result);
    return result;
}

trace_adaptor* get_adaptor() {
    trace_adaptor_params params;
    int write_syscall_nr = 1;
    params.amount_traced_syscalls = 1;
    params.traced_syscalls = &write_syscall_nr;
    params.interval_ms = 5000;
    params.scaling_metric = calc_metric;
    trace_adaptor* result = ta_create(&params);
    if (result == NULL) {
        debug_print("%s\n", "adaptor NULL");
    }
    return result;
}

void user_f(void *arg) {
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
    trace_adaptor* adaptor = get_adaptor();
    if (adaptor == NULL) {
        debug_print("%s\n", "could create traceset adaptor for tpool");
        return -1;
    }
    threadpool tm = tpool_create_2(num_threads, adaptor);
    debug_print("%s\n", "start submitting jobs to tpool");
    for (int i = 0; i < num_items; i++) {
        tpool_submit_job(tm, user_f, &i);
    }
    debug_print("%s\n", "waiting for tpool");
    tpool_wait(tm);
    debug_print("%s\n", "destroying tpool");
    tpool_destroy(tm);
    return 0;
}

int tracing_test() {
    trace_adaptor* adaptor = get_adaptor();
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

int main(int argc, char **argv) {
    debug_print("%s\n", "RUNNING TRACING.C TEST");
    debug_print("TRACING.C TEST RETURN: %d\n", tracing_test());
}





