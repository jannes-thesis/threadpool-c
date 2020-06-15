//
// Created by Jannes Timm on 04/02/2020.
//

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>

#include "spinlock_tpool.h"

static const size_t num_threads = 2;
static const size_t num_items = 100;

void worker(void *arg) {
    int *val = arg;
    int old = *val;

    *val += 1000;
    printf("tid=%p, old=%d, val=%d\n", pthread_self(), old, *val);

    if (*val % 2)
        usleep(100000);
}

typedef struct test_struct {
    _Atomic(int) atomic;
    int non_atomic;
} test_struct;

int test() {
    _Atomic(int) atomic;
    atomic_init(&atomic, 5);
    printf("%d\n", atomic);

    test_struct* s = malloc(sizeof(test_struct));
    s->non_atomic = 1;
    s->atomic = 1;
    printf("atomic: %d, non: %d\n", s->atomic, s->non_atomic);
    printf("atomic: %d, non: %d\n", atomic_load(&s->atomic), s->non_atomic);
}

int tpool_test() {
    threadpool tm;
    int* vals;
    size_t i;

    tm = tpool_create(num_threads);
    vals = calloc(num_items, sizeof(*vals));

    for (i = 0; i < num_items; i++) {
        vals[i] = i;
        tpool_submit_job(tm, worker, vals + i);
    }

    tpool_scale(tm, -1);
    tpool_wait(tm);

    for (i = 0; i < num_items; i++) {
        printf("%d\n", vals[i]);
    }

    free(vals);
    tpool_destroy(tm);
    return 0;
}

int main(int argc, char **argv) {
    return tpool_test();
}


