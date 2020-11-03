//
// Created by Jannes Timm on 30/01/2020.
//

#ifndef THREADPOOL_ADAPTIVE_TPOOL_H
#define THREADPOOL_ADAPTIVE_TPOOL_H

#include <stddef.h>
#include <stdbool.h>
#include "adapter.h"

// make this a parameter later
#define MAX_SIZE 64

// the thread pool
typedef struct tpool *threadpool;

// function that can be submitted
typedef void (*tfunc)(void *arg);

/**
 * @param size: initial size
 * @param adapter_params: parameters for adapter
 * when the adapter parameters are null, the pool will keep size static
 * @return
 */
threadpool tpool_create(size_t size, AdapterParameters *adapter_params);

// destroy pool, let all threads finish current work and then exit
void tpool_destroy(threadpool tpool);

// submit work to the pool
bool tpool_submit_job(threadpool tpool, tfunc f, void *f_arg);

// block until all work has been completed
void tpool_wait(threadpool tpool);

#endif
