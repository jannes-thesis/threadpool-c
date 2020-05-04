//
// Created by Jannes Timm on 30/01/2020.
//

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include "spinlock_tpool.h"

/* ================== data structures ==================== */

// lock values: -1 unlocked, 0 locked by API call, positive locked by worker

typedef struct job {
    tfunc function_ptr;
    void* function_arg_ptr;
    struct job* next;
} job;

typedef struct jobqueue {
    _Atomic(int) lock;
    job* first;
    job* last;
    size_t size;
} jobqueue;

typedef struct tpool {
    jobqueue jobqueue;
    _Atomic(int) count_lock;
    volatile size_t num_threads;
    volatile size_t num_busy_threads;
    bool stopping;      // this indicates a call to tpool_destroy has been issued
} tpool;

typedef struct worker {
    pid_t tid;
    pthread_t thread;
    struct worker* next;
} worker;

typedef struct worker_list {
    worker* first;
    worker* last;
    size_t size;
} worker_list;

typedef struct worker_args {
    tpool* tpool_ptr;
    pid_t* tid_ptr;
} worker_args;


/* ==================== Prototypes ==================== */

static void init_jobqueue(jobqueue* jq);
static job* pop_next_job(jobqueue* jobqueue_ptr);
static void push_new_job(jobqueue* jobqueue_ptr, job* new_job_ptr);
static job* create_job(tfunc fun_ptr, void* arg_ptr);
static void worker_function(worker_args* args);


/* ====================== API ====================== */

tpool* tpool_create(size_t size) {
    tpool* tpool_ptr;
    // initialize thread pool structure
    tpool_ptr = malloc(sizeof(tpool));
    if (tpool_ptr == NULL) {
        return NULL;
    }
    init_jobqueue(&(tpool_ptr->jobqueue));
    if (&(tpool_ptr->jobqueue) == NULL) {
        free(tpool_ptr);
        return NULL;
    }
    printf("queue initialized: %d\n", tpool_ptr->jobqueue.lock);
    tpool_ptr->num_threads = size;
    tpool_ptr->stopping = false;
    tpool_ptr->count_lock = -1;

    // create worker threads
    printf("creating workers\n");
    worker_list* workers = malloc(sizeof(worker_list));
    workers->size = size;
    worker* current = NULL;
    for (int i = 0; i < size; ++i) {
        printf("creating worker %d\n", i);
        worker* next = malloc(sizeof(worker));
        pthread_t thread;
        worker_args* worker_args_ptr = malloc(sizeof(worker_args));
        worker_args_ptr->tpool_ptr = tpool_ptr;
        worker_args_ptr->tid_ptr = &next->tid;
        printf("creating worker %d's pthread\n", i);
        pthread_create(&thread, NULL, (void* (*)(void*)) worker_function, (void*) worker_args_ptr);
        pthread_detach(thread);
        next->thread = thread;
        if (current != NULL) {
            current->next = next;
        } else {
            workers->first = next;
        }
        current = next;
    }
    workers->last = current;
    return tpool_ptr;
}

bool tpool_submit_job(tpool* tpool_ptr, tfunc tfunc_ptr, void* tfunc_arg_ptr) {
    if (tfunc_ptr == NULL) {
        return false;
    }
    // create job
    job* new_job_ptr = create_job(tfunc_ptr, tfunc_arg_ptr);
    if (new_job_ptr == NULL) {
        return false;
    }
    jobqueue* jobqueue_ptr = &(tpool_ptr->jobqueue);
    // grab lock on jobqueue
    int expected = -1;
    while (!atomic_compare_exchange_weak(&jobqueue_ptr->lock, &expected, 0)) {
        expected = -1;
    }
    // push job to queue
    push_new_job(jobqueue_ptr, new_job_ptr);
    // release lock
    jobqueue_ptr->lock = -1;
    return true;
}

/*
 * spins until all currently submitted jobs have been finished
 * no guarantee about jobs that are submitted after the call
 */
void tpool_wait(tpool* tpool_ptr) {
    // while there are still busy threads or the jobqueue is not empty wait
    while(tpool_ptr->num_busy_threads != 0 || tpool_ptr->jobqueue.size != 0) {}
}

void tpool_destroy(tpool* tpool_ptr) {
    if (tpool_ptr == NULL) return;

    jobqueue queue = tpool_ptr->jobqueue;
    jobqueue* jobqueue_ptr = &queue;
    tpool_ptr->stopping = true;

    // lock work queue and clear it
    int expected = -1;
    while (!atomic_compare_exchange_weak(&(queue.lock), &expected, 0)) {
        expected = -1;
    }
    job* to_free = queue.first;
    while (to_free != NULL) {
        job* new_to_free = to_free->next;
        free(to_free);
        to_free = new_to_free;
    }
    queue.size = 0;
    queue.lock = -1;

    // wait for all threads to be idle (in this case all must have exited)
    tpool_wait(tpool_ptr);
    // free datastructures
    free(tpool_ptr);
}

/* =================== Internal ===================== */

static job* create_job(tfunc fun_ptr, void* arg_ptr) {
    job* new_job = malloc(sizeof(job));
    if (new_job == NULL) {
        return NULL;
    }
    new_job->next = NULL;
    new_job->function_ptr = fun_ptr;
    new_job->function_arg_ptr = arg_ptr;
    return new_job;
}

static void init_jobqueue(jobqueue* jobqueue_ptr) {
    if (jobqueue_ptr == NULL) {
        return;
    }
    jobqueue_ptr->first = NULL;
    jobqueue_ptr->last = NULL;
    jobqueue_ptr->lock = -1;
//    atomic_init(&jobqueue_ptr->lock, -1);
//    printf("%s\n", atomic_load(&jobqueue_ptr->lock));
    printf("%d\n", jobqueue_ptr->lock);
    jobqueue_ptr->size = 0;
}

/*
 * pops the head of the jobqueue
 */
static job* pop_next_job(jobqueue* jobqueue_ptr) {
    printf("pop\n");
    job* head = jobqueue_ptr->first;
    if (head == NULL) {
        return NULL;
    }
    // update head (may be NULL if list empty now)
    jobqueue_ptr->first = head->next;
    // update last if list empty now
    if (jobqueue_ptr->first == NULL) {
        jobqueue_ptr->last = NULL;
    }
    // update size
    jobqueue_ptr->size -= 1;
    return head;
}

/*
 * pushes new job to the end of the jobqueue
 */
static void push_new_job(jobqueue* jobqueue_ptr, job* new_job) {
    printf("push\n");
    new_job->next = NULL;
    // if queue is empty new job becomes head and last
    if (jobqueue_ptr->size == 0) {
        jobqueue_ptr->first = new_job;
    }
    // otherwise the old last should point to the new job
    else {
        job* old_last = jobqueue_ptr->last;
        old_last->next = new_job;
    }
    jobqueue_ptr->last = new_job;
    jobqueue_ptr->size += 1;
}

static void worker_function(worker_args* args) {
    tpool* tpool_ptr = args->tpool_ptr;
    *args->tid_ptr = syscall(__NR_gettid);
    printf("worker starting, actual linux pid: %d\n", *args->tid_ptr);
    jobqueue* jobqueue_ptr = &(tpool_ptr->jobqueue);
    job* job_todo_ptr;
    int lock_available = -1;
    while (!tpool_ptr->stopping) {
        while (jobqueue_ptr->size == 0) {
            sleep(1);
            if (tpool_ptr->stopping)
                break;
        }
        while (!atomic_compare_exchange_weak(&(jobqueue_ptr->lock), &lock_available, *args->tid_ptr)) {
            lock_available = -1;
        }
        // --- jobqueue LOCKED

        // if thread pool is instructed to be destroyed, do not process next job, but exit
        if (tpool_ptr->stopping) {
            jobqueue_ptr->lock = -1;
            break;
        }
        // get next job
        printf("queue size: %zu\n", jobqueue_ptr->size);
        printf("worker %d popping job\n", *args->tid_ptr);
        job_todo_ptr = pop_next_job(jobqueue_ptr);
        jobqueue_ptr->lock = -1;
        // --- jobqueue UNLOCKED

        // check if really obtained job (queue could have been empty)
        if (job_todo_ptr != NULL) {
            // --- busy counter LOCKED
            while (!atomic_compare_exchange_weak(&(tpool_ptr->count_lock), &lock_available, *args->tid_ptr)) {
                lock_available = -1;
            }
            tpool_ptr->num_busy_threads += 1;
            tpool_ptr->count_lock = -1;
            // --- busy counter UNLOCKED
            printf("worker %d executing job\n", *args->tid_ptr);
            // execute job
            job_todo_ptr->function_ptr(job_todo_ptr->function_arg_ptr);
            // free
            free(job_todo_ptr);
            // decrease number of threads executing a job
            // --- busy counter LOCKED
            while (!atomic_compare_exchange_weak(&(tpool_ptr->count_lock), &lock_available, *args->tid_ptr)) {
                lock_available = -1;
            }
            tpool_ptr->num_busy_threads -= 1;
            tpool_ptr->count_lock = -1;
            // --- busy counter UNLOCKED
        }
    }
    while (!atomic_compare_exchange_weak(&(tpool_ptr->count_lock), &lock_available, *args->tid_ptr)) {
        lock_available = -1;
    }
    tpool_ptr->num_threads--;
    tpool_ptr->count_lock = -1;
}


