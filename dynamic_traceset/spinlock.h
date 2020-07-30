//
// Created by jannes on 30/07/2020.
//

#ifndef THREADPOOL_SPINLOCK_H
#define THREADPOOL_SPINLOCK_H

#include <stdatomic.h>

void spinlock_lock(_Atomic(int)* lock, int open_value, int close_value) {
    int expected = open_value;
    while (!atomic_compare_exchange_weak(lock, &expected, 0)) {
        expected = open_value;
    }
}

void spinlock_unlock(_Atomic(int)* lock, int open_value, int close_value) {
    if (*lock == close_value) {
        *lock = open_value;
    }
}

#endif //THREADPOOL_SPINLOCK_H
