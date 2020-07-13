//
// Created by Jannes Timm on 30/01/2020.
//

#ifndef THREADPOOL_ADAPTIVE_TPOOL_H
#define THREADPOOL_ADAPTIVE_TPOOL_H

#include <stddef.h>
#include <stdbool.h>
#include "scaling.h"

// the thread pool
typedef struct tpool* threadpool;

// function that can be submitted
typedef void (*tfunc)(void* arg);

// create pool with given size (amount of threads)
threadpool tpool_create(size_t size, adaptor_parameters* adaptor_params);

// create pool with completely initialized adaptor
threadpool tpool_create_2(size_t size, traceset_adaptor* adaptor);

// destroy pool, let all threads finish current work and then exit
void tpool_destroy(threadpool tpool);

// submit work to the pool
bool tpool_submit_job(threadpool tpool, tfunc f, void* f_arg);

// block until all work has been completed
void tpool_wait(threadpool tpool);

#endif
