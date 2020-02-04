#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

struct job {
    void *(*function_ptr)(void *);
    void *arg;
    struct job *next_job;
};
typedef struct job job_t;

struct thread_pool {
    job_t *head_job;
    pthread_mutex_t job_list_mutex;
};
typedef struct thread_pool tpool_t;

tpool_t * create_tpool() {
    tpool_t *tpool_ptr = malloc(sizeof(tpool_t));
    if (tpool_ptr == NULL) {
        return NULL;
    }
    tpool_ptr->head_job = NULL;
    pthread_mutex_t job_list_mutex = PTHREAD_MUTEX_INITIALIZER;
    tpool_ptr->job_list_mutex = job_list_mutex;
    return tpool_ptr;
}

void add_job(tpool_t *thread_pool, void *(*function)(void *), void *arg) {
    struct job new_job = {function, arg, thread_pool->head_job};
    thread_pool->head_job = &new_job;
}

void * printing() {
    printf("printing once\n");
    return (void *) 0;
}

int main() {
    printf("Hello, World!\n");
    int amount_threads = 10;
    pthread_t *tids = (pthread_t *) malloc(amount_threads * sizeof(pthread_t));
    void *arg;


    for (int i = 0; i < amount_threads; ++i) {
        pthread_create(&tids[i], NULL, printing, NULL);
    }
    for (int i = 0; i < amount_threads; ++i) {
        pthread_join(tids[i], NULL);
    }
    return 0;
}
