//
// Created by jannes on 23/04/2020.
//

#ifndef THREADPOOL_DYN_TPOOL_H
#define THREADPOOL_DYN_TPOOL_H

#endif //THREADPOOL_DYN_TPOOL_H


#include <stddef.h>
#include <stdbool.h>

// the thread pool
typedef struct tpool* dyn_threadpool;

// function that can be submitted
typedef void (*tfunc)(void* arg);

// create pool with given size (amount of threads)
dyn_threadpool tpool_create(size_t size);

// destroy pool, let all threads finish current work and then exit
void tpool_destroy(dyn_threadpool tpool);

// submit work to the pool
bool tpool_submit_job(dyn_threadpool tpool, tfunc f, void* f_arg);

// block until all work has been completed
void tpool_wait(dyn_threadpool tpool);
