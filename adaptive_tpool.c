#define _GNU_SOURCE
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <syscall.h>
#include "adaptive_tpool.h"
#include "adapter.h"
#include "debug_macro.h"

/* ================== data structures ==================== */

// lock values: -1 unlocked, 0 locked by API call, positive locked by worker

/* ----------- work queue --------------- */
typedef struct user_function
{
    tfunc f;
    void *arg;
} user_function;

typedef enum scaling_command
{
    Clone,
    Terminate
} scaling_command;

typedef union work_item
{
    user_function uf;
    scaling_command sc;
} work_item;

typedef struct job
{
    bool is_uf;
    work_item wi;
    struct job *next;
} job;

typedef struct jobqueue
{
    pthread_spinlock_t lock;
    job *first;
    job *last;
    size_t size;
} jobqueue;

/* ------------ pool + workers --------------*/
typedef struct worker
{
    size_t wid;
    pthread_t thread;
    struct worker *next;
} worker;

typedef struct worker_list
{
    pthread_spinlock_t lock;
    worker *first;
    worker *last;
    size_t amount;
    size_t max_id;
} worker_list;

typedef struct tpool
{
    jobqueue jobqueue;
    /** lock for thread counter and busy thread counter */
    pthread_spinlock_t count_lock;
    volatile size_t num_threads;
    volatile size_t num_busy_threads;
    /** true once destroy call has been issued */
    bool stopping;
    worker_list workers;
    bool is_static;
    /** pid of the process that created the pool,
     * for logging purposes */
    pid_t creator_pid;
} tpool;

typedef struct worker_args
{
    tpool *tp;
    size_t wid;
} worker_args;

/* ==================== Prototypes ==================== */

static void init_jobqueue(jobqueue *jq);

static job *pop_next_job(jobqueue *jq);

static void push_new_job(jobqueue *jq, job *new_job);

static job *create_user_job(tfunc ufunc, void *uarg);

static job *create_scale_job(scaling_command sc);

static void check_scaling(tpool *tp, size_t wid);

static void push_scale_job(jobqueue *jq, scaling_command sc);

static void worker_function(worker_args *args);

static void add_extra_worker(tpool *tpool_ptr);

static void remove_worker(size_t worker_id, tpool *tpool_ptr);

/* ====================== API ====================== */

tpool *tpool_create(size_t size, AdapterParameters *adaptor_params, const char *adapter_algo_params)
{
    tpool *tpool_ptr;
    // initialize thread pool structure
    tpool_ptr = malloc(sizeof(tpool));
    if (tpool_ptr == NULL)
    {
        return NULL;
    }
    tpool_ptr->creator_pid = syscall(__NR_gettid);
    if (adaptor_params == NULL)
    {
        tpool_ptr->is_static = true;
    }
    else
    {
        if (!new_adapter(adaptor_params, adapter_algo_params))
        {
            free(tpool_ptr);
            return NULL;
        }
        tpool_ptr->is_static = false;
    }
    // initialize all spinlocks
    if (pthread_spin_init(&tpool_ptr->count_lock, PTHREAD_PROCESS_PRIVATE) + pthread_spin_init(&tpool_ptr->jobqueue.lock, PTHREAD_PROCESS_PRIVATE) + pthread_spin_init(&tpool_ptr->workers.lock, PTHREAD_PROCESS_PRIVATE) != 0)
    {
        // TODO: proper error handling
        return NULL;
    }

    init_jobqueue(&(tpool_ptr->jobqueue));
    if (&(tpool_ptr->jobqueue) == NULL)
    {
        free(tpool_ptr);
        return NULL;
    }
    debug_print("queue initialized: %d\n", tpool_ptr->jobqueue.lock);
    tpool_ptr->num_threads = size;
    tpool_ptr->stopping = false;

    // create worker threads
    debug_print("%s", "creating workers\n");
    tpool_ptr->workers.amount = size;
    tpool_ptr->workers.max_id = size - 1;
    worker *current = NULL;

    for (int i = 0; i < size; ++i)
    {
        pthread_t thread;
        debug_print("creating worker %d\n", i);
        worker *next = malloc(sizeof(worker));
        next->wid = i;

        worker_args *worker_args_ptr = malloc(sizeof(worker_args));
        worker_args_ptr->tp = tpool_ptr;
        worker_args_ptr->wid = next->wid;

        debug_print("creating worker %d's pthread\n", i);
        pthread_create(&thread, NULL, (void *(*)(void *))worker_function, (void *)worker_args_ptr);
        pthread_detach(thread);
        next->thread = thread;
        if (current != NULL)
        {
            current->next = next;
        }
        else
        {
            tpool_ptr->workers.first = next;
        }
        current = next;
    }
    tpool_ptr->workers.last = current;
    return tpool_ptr;
}

bool tpool_submit_job(tpool *tpool_ptr, tfunc tfunc_ptr, void *tfunc_arg_ptr)
{
    if (tfunc_ptr == NULL)
    {
        return false;
    }
    // create job
    job *new_job_ptr = create_user_job(tfunc_ptr, tfunc_arg_ptr);
    if (new_job_ptr == NULL)
    {
        return false;
    }
    jobqueue *jobqueue_ptr = &(tpool_ptr->jobqueue);
    // grab lock on jobqueue
    pthread_spin_lock(&jobqueue_ptr->lock);
    // push job to queue
    push_new_job(jobqueue_ptr, new_job_ptr);
    // release lock
    pthread_spin_unlock(&jobqueue_ptr->lock);
    return true;
}

/*
 * spins until all currently submitted jobs have been finished
 * no guarantee about jobs that are submitted after the call
 */
void tpool_wait(tpool *tpool_ptr)
{
    // while there are still busy threads or the jobqueue is not empty wait
    while (tpool_ptr->num_busy_threads != 0 || tpool_ptr->jobqueue.size != 0)
    {
        sleep(1);
    }
}

void tpool_destroy(tpool *tpool_ptr)
{
    if (tpool_ptr == NULL)
        return;

    jobqueue queue = tpool_ptr->jobqueue;
    tpool_ptr->stopping = true;

    // lock work queue and clear it
    pthread_spin_lock(&queue.lock);
    job *to_free = queue.first;
    while (to_free != NULL)
    {
        job *new_to_free = to_free->next;
        free(to_free);
        to_free = new_to_free;
    }
    queue.size = 0;
    pthread_spin_unlock(&queue.lock);

    // wait for all threads to be idle (in this case all must have exited)
    tpool_wait(tpool_ptr);
    // free datastructures
    free(tpool_ptr);
}

// NO ERROR HANDLING YET
bool tpool_scale(tpool *tp, int diff)
{
    // don't go below one worker thread
    if (tp->num_threads + diff <= 0)
    {
        debug_print("%s\n", "scaling down would drop below 1 worker, only scale down to 1 worker");
        diff = 1 - tp->num_threads;
    }
    // don't go above max worker amount
    else if (tp->num_threads + diff > MAX_SIZE)
    {
        debug_print("%s\n", "scaling up would go above max amount workers, only scale to max");
        diff = MAX_SIZE - tp->num_threads;
    }
    // grab lock on jobqueue
    debug_print("tpool (creator pid: %d) scaling to %d\n", tp->creator_pid, ((int) tp->num_threads) + diff);
    pthread_spin_lock(&tp->jobqueue.lock);
    if (diff < 0)
    {
        while (diff < 0)
        {
            push_scale_job(&tp->jobqueue, Terminate);
            diff++;
        }
    }
    else
    {
        while (diff > 0)
        {
            push_scale_job(&tp->jobqueue, Clone);
            diff--;
        }
    }
    // release lock
    pthread_spin_unlock(&tp->jobqueue.lock);
    return true;
}

/* =================== Internal ===================== */

static job *create_user_job(tfunc ufunc, void *uarg)
{
    job *new_job = malloc(sizeof(job));
    if (new_job == NULL)
    {
        return NULL;
    }
    new_job->next = NULL;
    new_job->is_uf = true;
    new_job->wi.uf.f = ufunc;
    new_job->wi.uf.arg = uarg;
    return new_job;
}

static job *create_scale_job(scaling_command sc)
{
    job *new_job = malloc(sizeof(job));
    if (new_job == NULL)
    {
        return NULL;
    }
    new_job->next = NULL;
    new_job->is_uf = false;
    new_job->wi.sc = sc;
    return new_job;
}

static void init_jobqueue(jobqueue *jq)
{
    if (jq == NULL)
    {
        return;
    }
    jq->first = NULL;
    jq->last = NULL;
    jq->size = 0;
}

/*
 * pops the head of the jobqueue
 */
static job *pop_next_job(jobqueue *jq)
{
    debug_print("%s", "pop\n");
    job *head = jq->first;
    if (head == NULL)
    {
        return NULL;
    }
    // update head (may be NULL if list empty now)
    jq->first = head->next;
    // update last if list empty now
    if (jq->first == NULL)
    {
        jq->last = NULL;
    }
    // update size
    jq->size -= 1;
    return head;
}

/*
 * pushes new job to the end of the jobqueue
 */
static void push_new_job(jobqueue *jq, job *new_job)
{
    //    debug_print("%s", "push\n");
    new_job->next = NULL;
    // if queue is empty new job becomes head and last
    if (jq->size == 0)
    {
        jq->first = new_job;
    }
    // otherwise the old last should point to the new job
    else
    {
        job *old_last = jq->last;
        old_last->next = new_job;
    }
    jq->last = new_job;
    jq->size += 1;
}

static void push_scale_job(jobqueue *jq, scaling_command sc)
{
    job *scj = create_scale_job(sc);
    scj->next = jq->first;
    jq->first = scj;
    jq->size += 1;
}

static unsigned long current_time_ms()
{
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
    unsigned long ms = (unsigned long)(spec.tv_nsec / 1.0e6) + (1000 * spec.tv_sec);
    return ms;
}

/**
 * check if pool needs to scale and add/remove worker accordingly
 * @param worker_id
 * @param tpool_ptr
 * @return true if worker should exit
 */
static void check_scaling(tpool *tpool_ptr, size_t wid)
{
    debug_print("worker %zu get scaling advice\n", wid);
    int to_scale = get_scaling_advice();
    debug_print("worker %zu got scaling advice: scale by %d\n", wid, to_scale);
    if (to_scale != 0)
    {
        debug_print("worker %zu SCALING now: %lu (by %d)\n", wid, current_time_ms(), to_scale);
        tpool_scale(tpool_ptr, to_scale);
    }
    else
    {
        debug_print("worker %zu no scaling\n", wid);
    }
}

static void worker_function(worker_args *args)
{
    char thread_name[20];
    sprintf(thread_name, "worker-%zu", args->wid);
    tpool *tpool_ptr = args->tp;
    pid_t worker_pid = syscall(__NR_gettid);
    debug_print("worker %zu starting (pid: %d)\n", args->wid, worker_pid);
    pthread_setname_np(pthread_self(), thread_name);
    if (!tpool_ptr->is_static)
    {
        add_tracee(worker_pid);
        debug_print("worker %zu added as tracee (pid: %d)\n", args->wid, worker_pid);
    }
    jobqueue *jobqueue_ptr = &(tpool_ptr->jobqueue);
    job *job_todo;
    while (!tpool_ptr->stopping)
    {
        if (!tpool_ptr->is_static)
            check_scaling(tpool_ptr, args->wid);
        while (jobqueue_ptr->size == 0)
        {
            sleep(1);
            if (!tpool_ptr->is_static)
                check_scaling(tpool_ptr, args->wid);
            if (tpool_ptr->stopping)
                break;
        }
        /* LOCK jobqueue */
        pthread_spin_lock(&jobqueue_ptr->lock);
        // if thread pool is instructed to be destroyed, do not process next job, but exit
        if (tpool_ptr->stopping)
        {
            jobqueue_ptr->lock = -1;
            break;
        }
        // get next job
        debug_print("queue size: %zu\n", jobqueue_ptr->size);
        debug_print("worker %zu popping job\n", args->wid);
        job_todo = pop_next_job(jobqueue_ptr);
        pthread_spin_unlock(&jobqueue_ptr->lock);
        /* UNLOCKED jobqueue */

        // check if really obtained job (queue could have been empty)
        if (job_todo != NULL && job_todo->is_uf)
        {
            // --- busy counter LOCKED
            pthread_spin_lock(&tpool_ptr->count_lock);
            tpool_ptr->num_busy_threads += 1;
            pthread_spin_unlock(&tpool_ptr->count_lock);
            // --- busy counter UNLOCKED
            debug_print("worker %zu executing job\n", args->wid);
            // execute job
            job_todo->wi.uf.f(job_todo->wi.uf.arg);
            // free
            free(job_todo);
            // decrease number of threads executing a job
            // --- busy counter LOCKED
            pthread_spin_lock(&tpool_ptr->count_lock);
            tpool_ptr->num_busy_threads -= 1;
            pthread_spin_unlock(&tpool_ptr->count_lock);
            // --- busy counter UNLOCKED
        }
        else if (job_todo != NULL)
        {
            if (job_todo->wi.sc == Clone)
            {
                debug_print("worker %zu performing clone\n", args->wid);
                pthread_spin_lock(&tpool_ptr->workers.lock);
                add_extra_worker(tpool_ptr);
                pthread_spin_unlock(&tpool_ptr->workers.lock);
            }
            else if (job_todo->wi.sc == Terminate)
            {
                debug_print("worker %zu performing terminate\n", args->wid);
                break;
            }
        }
    }
    // remove from workers list
    pthread_spin_lock(&tpool_ptr->workers.lock);
    remove_worker(args->wid, tpool_ptr);
    pthread_spin_unlock(&tpool_ptr->workers.lock);
    // update active thread amount
    pthread_spin_lock(&tpool_ptr->count_lock);
    tpool_ptr->num_threads--;
    pthread_spin_unlock(&tpool_ptr->count_lock);
    if (!tpool_ptr->is_static)
        remove_tracee(worker_pid);
}

/**
 * worker list must be locked/unlocked by caller
 * @param worker_id
 * @param tpool_ptr
 */
static void remove_worker(size_t worker_id, tpool *tpool_ptr)
{
    worker *current = tpool_ptr->workers.first;
    worker *last = NULL;
    while (current != NULL)
    {
        if (current->wid == worker_id)
        {
            // if first
            if (last == NULL)
            {
                tpool_ptr->workers.first = current->next;
            }
            else
            {
                last->next = current->next;
            }
            // if last
            if (current->next == NULL)
            {
                tpool_ptr->workers.last = last;
            }
            free(current);
            tpool_ptr->workers.amount -= 1;
            break;
        }
        last = current;
        current = current->next;
    }
}

/**
 * worker list must be locked/unlocked by caller
 * @param tpool_ptr
 */
static void add_extra_worker(tpool *tpool_ptr)
{
    pthread_t thread;
    worker *new_worker = malloc(sizeof(worker));
    new_worker->wid = tpool_ptr->workers.max_id + 1;
    new_worker->next = NULL;

    debug_print("creating worker %zu\n", new_worker->wid);
    worker_args *worker_args_ptr = malloc(sizeof(worker_args));
    worker_args_ptr->tp = tpool_ptr;
    worker_args_ptr->wid = new_worker->wid;

    debug_print("creating worker %zu's pthread\n", new_worker->wid);
    pthread_create(&thread, NULL, (void *(*)(void *))worker_function, (void *)worker_args_ptr);
    pthread_detach(thread);
    new_worker->thread = thread;

    pthread_spin_lock(&tpool_ptr->count_lock);
    tpool_ptr->num_threads += 1;
    pthread_spin_unlock(&tpool_ptr->count_lock);

    // append worker to worker list
    tpool_ptr->workers.last->next = new_worker;
    tpool_ptr->workers.last = new_worker;
    tpool_ptr->workers.amount += 1;
    tpool_ptr->workers.max_id = new_worker->wid;
}
