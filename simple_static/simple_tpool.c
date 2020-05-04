//
// Created by Jannes Timm on 30/01/2020.
//

#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include "simple_tpool.h"

/* ================== data structures ==================== */

typedef struct job {
    tfunc function_ptr;
    void* function_arg_ptr;
    struct job* next;
} job;

typedef struct jobqueue {
    pthread_mutex_t access_mutex;
    pthread_cond_t non_empty_cond;
    job* first;
    job* last;
    size_t size;
} jobqueue;

typedef struct tpool {
    jobqueue jobqueue;
    volatile size_t num_threads;
    volatile size_t num_busy_threads;
    pthread_mutex_t thread_count_mutex;
    pthread_cond_t all_threads_idle_cond;
    bool stopping;      // this indicates a call to tpool_destroy has been issued
} tpool;


/* ==================== Prototypes ==================== */

static void init_jobqueue(jobqueue* jq);
static job* pop_next_job(jobqueue* jobqueue_ptr);
static void push_new_job(jobqueue* jobqueue_ptr, job* new_job_ptr);
static job* create_job(tfunc fun_ptr, void* arg_ptr);
static void worker_function(tpool* tpool_ptr);


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
    pthread_cond_init(&(tpool_ptr->all_threads_idle_cond), NULL);
    tpool_ptr->num_threads = size;
    tpool_ptr->stopping = false;

    // create threads
    for (int i = 0; i < size; ++i) {
        pthread_t thread;
        pthread_create(&thread, NULL, (void *(*)(void *)) worker_function, tpool_ptr);
        pthread_detach(thread);
    }

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
    // acquire jobqueue mutex
    pthread_mutex_t* jq_mutex_ptr = &(jobqueue_ptr->access_mutex);
    pthread_mutex_lock(jq_mutex_ptr);
    // push job to queue
    push_new_job(jobqueue_ptr, new_job_ptr);
    // broadcast on the jobqueue condition variable to inform that queue is non-empty
    pthread_cond_broadcast(&(jobqueue_ptr->non_empty_cond));
    // unlock mutex and return
    pthread_mutex_unlock(jq_mutex_ptr);
    return true;
}

/*
 * blocks until all currently submitted jobs have been finished
 * no guarantee about jobs that are submitted after the call
 */
void tpool_wait(tpool* tpool_ptr) {
    pthread_mutex_t* tpool_busy_mutex_ptr = &(tpool_ptr->thread_count_mutex);
    pthread_cond_t* tpool_idle_cond_ptr = &(tpool_ptr->all_threads_idle_cond);
    // lock busy count mutex
    pthread_mutex_lock(tpool_busy_mutex_ptr);
    // while there are still busy threads and the jobqueue is not empty conditionally wait
    while(tpool_ptr->num_busy_threads != 0 || tpool_ptr->jobqueue.size != 0) {
        pthread_cond_wait(tpool_idle_cond_ptr, tpool_busy_mutex_ptr);
    }
    pthread_mutex_unlock(tpool_busy_mutex_ptr);
}

void tpool_destroy(tpool* tpool_ptr) {
    if (tpool_ptr == NULL) return;

    jobqueue* queue_ptr = &tpool_ptr->jobqueue;
    pthread_mutex_t* queue_mutex_ptr = &(queue_ptr->access_mutex);
    pthread_cond_t* queue_cond_ptr = &(queue_ptr->non_empty_cond);

    tpool_ptr->stopping = true;

    printf("destroy: clear queue\n");
    // lock work queue and clear it
    pthread_mutex_lock(queue_mutex_ptr);
    job* to_free = queue_ptr->first;
    while (to_free != NULL) {
        job* new_to_free = to_free->next;
        free(to_free);
        to_free = new_to_free;
    }
    printf("destroy: signal blocked threads\n");
    // signal working threads that are still alive (and waiting on queue)
    pthread_cond_broadcast(queue_cond_ptr);
    pthread_mutex_unlock(queue_mutex_ptr);

    // wait for all threads to be idle (in this case all must have exited)
    tpool_wait(tpool_ptr);

    // free datastructures
    pthread_mutex_destroy(queue_mutex_ptr);
    pthread_cond_destroy(queue_cond_ptr);
    pthread_mutex_destroy(&(tpool_ptr->thread_count_mutex));
    pthread_cond_destroy(&(tpool_ptr->all_threads_idle_cond));
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
    pthread_mutex_init(&(jobqueue_ptr->access_mutex), NULL);
    pthread_cond_init(&(jobqueue_ptr->non_empty_cond), NULL);
    jobqueue_ptr->size = 0;
}

/*
 * pops the head of the jobqueue
 */
static job* pop_next_job(jobqueue* jobqueue_ptr) {
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

static void worker_function(tpool* tpool_ptr) {
    jobqueue *jobqueue_ptr = &(tpool_ptr->jobqueue);
    pthread_mutex_t* jobqueue_access_mutex_ptr = &(jobqueue_ptr->access_mutex);
    pthread_mutex_t* thread_count_mutex_ptr = &(tpool_ptr->thread_count_mutex);
    job* job_todo_ptr;
    while (1) {
        // --- jobqueue LOCKED
        pthread_mutex_lock(jobqueue_access_mutex_ptr);
        // continue conditionally on another job being available
        while (jobqueue_ptr->first == NULL && !tpool_ptr->stopping) {
            printf("worker: wait on empty queue\n");
            pthread_cond_wait(&(jobqueue_ptr->non_empty_cond), &(jobqueue_ptr->access_mutex));
            printf("worker: wake up on non-empty queue\n");
        }
        // if thread pool is instructed to be destroyed, do not process next job, but exit
        if (tpool_ptr->stopping) {
            break;
        }
        // get next job
        job_todo_ptr = pop_next_job(jobqueue_ptr);
        pthread_mutex_unlock(jobqueue_access_mutex_ptr);
        // --- jobqueue UNLOCKED

        // increase the number of busy threads (may be invalidated right again)
        // --- busy counter LOCKED
        pthread_mutex_lock(thread_count_mutex_ptr);
        tpool_ptr->num_busy_threads += 1;
        pthread_mutex_unlock(thread_count_mutex_ptr);
        // --- busy counter UNLOCKED

        // check if really obtained job (could have been obtained by other workers)
        if (job_todo_ptr != NULL) {
            // execute job
            job_todo_ptr->function_ptr(job_todo_ptr->function_arg_ptr);
            // free
            free(job_todo_ptr);
        }

        // decrease number of threads executing a job
        // --- busy counter LOCKED
        pthread_mutex_lock(thread_count_mutex_ptr);
        tpool_ptr->num_busy_threads -= 1;
        // if thread pool not instructed to stop, no threads are busy and work list is empty
        // ----> signal on all threads idle condition
        if (!(tpool_ptr->stopping) && tpool_ptr->num_busy_threads == 0 && jobqueue_ptr->first == NULL) {
            pthread_cond_signal(&(tpool_ptr->all_threads_idle_cond));
        }
        pthread_mutex_unlock(thread_count_mutex_ptr);
        // --- busy counter UNLOCKED
    }
    printf("worker exiting\n");
    // wake up workers blocked on queue & release mutex
    pthread_cond_broadcast(&jobqueue_ptr->non_empty_cond);
    pthread_mutex_unlock(jobqueue_access_mutex_ptr);
    // decrement thread counter and signal all thread idle if count is zero
    pthread_mutex_lock(thread_count_mutex_ptr);
    tpool_ptr->num_threads--;
    if (tpool_ptr->num_threads == 0) {
        pthread_cond_signal(&(tpool_ptr->all_threads_idle_cond));
    }
    pthread_mutex_unlock(thread_count_mutex_ptr);
}


