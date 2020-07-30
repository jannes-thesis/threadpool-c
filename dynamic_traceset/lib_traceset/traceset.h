//
// Created by jannes on 09/07/2020.
//

#ifndef SYSCALL_TESTS_TRACESET_H
#define SYSCALL_TESTS_TRACESET_H

#include <linux/types.h>
#include <sys/types.h>

typedef struct __traceset_data {
    int traceset_id;
    __u32 amount_targets;
    __u64 read_bytes;
    __u64 write_bytes;
    // __u32 amount_syscalls; NOT NEEDEED
    /* struct traceset_syscall_data syscalls_data[]; */
    /*
     * c99 flexible arrary member notation, not supported in kernel (yet)
     * instead just put the array right behind traceset_data struct in the page
     * size is known by caller (amount of system calls to do accounting for)
     */
} traceset_data;

typedef struct __traceset_syscall_data {
    __u32 count;
    __u64 total_time;
} traceset_syscall_data;

typedef struct traceset {
    struct __traceset_data* data;
    int amount_syscalls;
    int* syscall_nrs;
    struct __traceset_syscall_data* sdata_arr;
} traceset;

traceset* register_traceset(pid_t* target_pids, int amount_targets, int* syscall_nrs, int amount_syscalls);
void free_traceset(traceset* tset);
int deregister_traceset(int traceset_id);
int register_traceset_targets(int traceset_id, pid_t* target_pids, int amount_targets);
int deregister_traceset_targets(int traceset_id, pid_t* target_pids, int amount_targets);
bool register_traceset_target(int traceset_id, pid_t target_pid);
bool deregister_traceset_target(int traceset_id, pid_t target_pid);

#endif

