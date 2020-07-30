//
// Created by jannes on 09/07/2020.
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <sys/types.h>
#include <string.h>
#include "traceset.h"
#include "debug_macro.h"

static traceset_syscall_data* get_syscall_datap(traceset_data* datap) {
    return (traceset_syscall_data*) (datap + 1);
}

traceset* register_traceset(pid_t* target_pids, int amount_targets, int* syscall_nrs, int amount_syscalls) {
    traceset_data* datap;
    traceset_syscall_data* syscall_datap;
    traceset* ts = malloc(sizeof(traceset));
    if (ts == NULL) {
        return NULL;
    }

    int register_return = (int) syscall(436, -1, target_pids, amount_targets, syscall_nrs, amount_syscalls);
    if (register_return < 0) {
        debug_print("register traceset returned error: %d\n", register_return);
        free(ts);
        return NULL;
    }
    else {
        debug_print("register traceset returned file descriptor: %d\n", register_return);
        datap = mmap(0, sizeof(traceset_data), PROT_READ | PROT_WRITE,
                     MAP_SHARED, register_return, 0);
        if (datap == NULL) {
            free(ts);
            return NULL;
        }
        syscall_datap = get_syscall_datap(datap);
        ts->amount_syscalls = amount_syscalls;
        ts->syscall_nrs = syscall_nrs;
        ts->data = datap;
        ts->sdata_arr = syscall_datap;
        return ts;
    }
}

void free_traceset(traceset* tset) {
    deregister_traceset(tset->data->traceset_id);
    munmap(tset->data, sizeof(traceset_data));
    free(tset->syscall_nrs);
    free(tset);
}

int deregister_traceset(int traceset_id) {
    return (int) syscall(437, traceset_id, NULL, -1);
}

int register_traceset_targets(int traceset_id, pid_t* target_pids, int amount_targets) {
    return (int) syscall(436, traceset_id, target_pids, amount_targets, 0, NULL);
}

int deregister_traceset_targets(int traceset_id, pid_t* target_pids, int amount_targets) {
    return (int) syscall(437, traceset_id, target_pids, amount_targets, 0, NULL);
}

bool register_traceset_target(int traceset_id, pid_t target_pid) {
    if (register_traceset_targets(traceset_id, &target_pid, 1) == 0)
        return true;
    else
        return false;
}

bool deregister_traceset_target(int traceset_id, pid_t target_pid) {
    if (deregister_traceset_targets(traceset_id, &target_pid, 1) == 1)
        return true;
    else
        return false;
}

