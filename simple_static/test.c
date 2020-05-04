//
// Created by Jannes Timm on 04/02/2020.
//

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#include "simple_tpool.h"

static const size_t num_threads = 2;
static const size_t num_items = 10;

void worker(void *arg) {
    int *val = arg;
    int old = *val;

    *val += 1000;
    printf("tid=%lu, old=%d, val=%d\n", pthread_self(), old, *val);

    if (*val % 2)
        usleep(100000);
}

int main(int argc, char **argv) {
    threadpool tm;
    int* vals;
    size_t i;

    tm = tpool_create(num_threads);
    vals = calloc(num_items, sizeof(*vals));

    sleep(5);
    for (i = 0; i < num_items; i++) {
        vals[i] = i;
        tpool_submit_job(tm, worker, vals + i);
    }

    printf("wait for all jobs to be processed\n");
    tpool_wait(tm);

//    for (i = 0; i < num_items; i++) {
//        printf("%d\n", vals[i]);
//    }

    free(vals);
    printf("destroy pool\n");
    tpool_destroy(tm);
    printf("destroyed pool\n");
    return 0;
}


