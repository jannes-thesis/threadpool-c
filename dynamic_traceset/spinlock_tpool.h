//
// Created by Jannes Timm on 30/01/2020.
//

#ifndef THREADINGC_SIMPLE_TPOOL_H
#define THREADINGC_SIMPLE_TPOOL_H
#endif //THREADINGC_SIMPLE_TPOOL_H

#include <stddef.h>
#include <stdbool.h>

// the thread pool
typedef struct tpool* threadpool;

// function that can be submitted
typedef void (*tfunc)(void* arg);

// create pool with given size (amount of threads)
threadpool tpool_create(size_t size);

// destroy pool, let all threads finish current work and then exit
void tpool_destroy(threadpool tpool);

// submit work to the pool
bool tpool_submit_job(threadpool tpool, tfunc f, void* f_arg);

// block until all work has been completed
void tpool_wait(threadpool tpool);

// scale amount of workers by diff
bool tpool_scale(threadpool tpool, int diff);

void set_scale_val(threadpool tpool, int val);

