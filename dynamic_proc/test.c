//
// Created by Jannes Timm on 04/02/2020.
//

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#include "dyn_tpool.h"

static const size_t num_threads = 4;
static const size_t num_items = 100;

void worker(void* arg) {
    FILE* fp;
    pthread_t own_id = pthread_self();
    char filename[128];
    sprintf(filename, "thread%lu.txt", own_id);
    int* val = arg;
    int old = *val;

    *val += 1000;
    fp = fopen(filename, "a");
    if (fp == NULL) {
        printf("shouldnt happen");
    }
    for (int i = 0; i < 1000; i++) {
        fprintf(fp, "tid=%lu, old=%d, val=%d\n", own_id, old, *val);
    }
    fflush(fp);
    fclose(fp);

    if (*val % 2)
        usleep(100000);
}

int main(int argc, char** argv) {
    dyn_threadpool tm;
    int* vals;
    size_t i;

    tm = tpool_create(num_threads);
    vals = calloc(num_items, sizeof(*vals));

    for (i = 0; i < num_items; i++) {
        vals[i] = i;
        tpool_submit_job(tm, worker, vals + i);
    }

    printf("wait for all jobs to finish\n");
    tpool_wait(tm);

    for (i = 0; i < num_items; i++) {
        printf("%d\n", vals[i]);
    }

    free(vals);
    tpool_destroy(tm);
//    int val = 1;
//    worker(&val);
    return 0;
}


