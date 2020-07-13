//
// Created by jannes on 13-07-20.
//

#ifndef THREADPOOL_DEBUG_MACRO_H
#define THREADPOOL_DEBUG_MACRO_H

#define DEBUG 1
#define debug_print(fmt, ...) \
        do { if (DEBUG) fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, \
                                __LINE__, __func__, __VA_ARGS__); } while (0)

#endif //THREADPOOL_DEBUG_MACRO_H
