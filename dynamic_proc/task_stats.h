//
// Created by jannes on 23/04/2020.
//

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef THREADPOOL_TASK_STATS_H
#define THREADPOOL_TASK_STATS_H

#endif //THREADPOOL_TASK_STATS_H

typedef struct io_stats {
    unsigned long long read_bytes;
    unsigned long long write_bytes;
} io_stats;

io_stats* get_task_io_stats(pid_t tid);