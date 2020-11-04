#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/wait.h>

#include "debug_macro.h"
#include "adapter.h"
#include "adaptive_tpool.h"

char *OUTPUT_DIR;
bool exiting = false;

IntervalDerivedData calc_metrics(const IntervalDataFFI *data)
{
    IntervalDerivedData derived;
    derived.reset_metric = (double)data->write_bytes;
    derived.scale_metric = (double)data->write_bytes;
    return derived;
}

AdapterParameters *get_adapter_params()
{
    AdapterParameters *params = malloc(sizeof(AdapterParameters));
    int32_t *syscalls = malloc(sizeof(int32_t));
    syscalls[0] = 1;

    params->amount_syscalls = 1;
    params->calc_interval_metrics = calc_metrics;
    params->check_interval_ms = 1000;
    params->syscall_nrs = syscalls;
    return params;
}

void background_writer(void *arg)
{
    int *id = arg;
    char filename[50];
    debug_print("%s %d\n", "background writer function start", *id);
    sprintf(filename, "%s/backwout%d", OUTPUT_DIR, *id);
    while (!exiting)
    {
        FILE *fp = fopen(filename, "w");
        int fd = fileno(fp);
        // roughly 100Kb
        for (int i = 0; i < 7000; ++i)
        {
            if (exiting)
            {
                break;
            }
            fputs("this is a line\n", fp);
            fsync(fd);
        }
        remove(filename);
    }
}

void spawn_background_writer(void *id)
{
    pthread_t background_thread;
    pthread_create(&background_thread, NULL, (void *(*)(void *))background_writer, (void *)id);
}

/**
 * writes 100kb file line by line with fsyncing every line
 */
void worker_write_synced(void *arg)
{
    int *valp = arg;
    debug_print("%s %d\n", "user function start", *valp);
    char filename[50];
    sprintf(filename, "%s/wout%d", OUTPUT_DIR, *valp);
    FILE *fp = fopen(filename, "w");
    int fd = fileno(fp);
    // roughly 100Kb
    for (int i = 0; i < 7000; ++i)
    {
        fputs("this is a line\n", fp);
        fsync(fd);
    }
    debug_print("%s %d\n", "user function end", *valp);
}

/**
 * reads an input file line by line using a single 4kb buffer
 */
void worker_read_buffered(void *arg)
{
    int *valp = arg;
    char filename[50];
    char buffer[4096];
    debug_print("%s %d\n", "buffered read function start", *valp);
    sprintf(filename, "%s/rin%d", OUTPUT_DIR, *valp);
    debug_print("%s %d\n", "buffered read printed filename", *valp);
    FILE *fp = fopen(filename, "r");
    debug_print("%s %d\n", "buffered read opened file", *valp);
    if (fp == NULL)
    {
        debug_print("%s\n", "file nULL");
    }
    while (fgets(buffer, 4096, fp) != NULL)
    {
        debug_print("%s %d\n", "buffered read", *valp);
    }
    debug_print("%s %d\n", "buffered read function end", *valp);
}

threadpool get_tpool(int pool_size)
{
    debug_print("%s\n", "creating tpool");
    if (pool_size > 0)
    {
        return tpool_create(pool_size, NULL);
    }
    else
    {
        AdapterParameters *adapter_params = get_adapter_params();
        return tpool_create(1, adapter_params);
    }
}

void delete_files(int num_items)
{
    char filename[50];
    debug_print("%s\n", "deleting worker output");
    for (int j = 0; j < num_items; j++)
    {
        sprintf(filename, "%s/wout%d", OUTPUT_DIR, j);
        remove(filename);
    }
}

void tpool_wait_destroy(threadpool tpool)
{
    debug_print("%s\n", "waiting for tpool");
    tpool_wait(tpool);
    debug_print("%s\n", "destroying tpool");
    tpool_destroy(tpool);
}

/**
 * a test run where the work queue is filled at maximum rate
 * @param pool_size if 0: adaptive pool, otherwise: static
 */
void static_load(int pool_size, int num_items, void *worker_function)
{
    int is[num_items];
    threadpool tpool = get_tpool(pool_size);

    debug_print("%s\n", "start submitting jobs to tpool");
    for (int i = 0; i < num_items; i++)
    {
        is[i] = i;
        tpool_submit_job(tpool, worker_function, &is[i]);
    }

    tpool_wait_destroy(tpool);
    delete_files(num_items);
}

/**
 * a test run where the items are submitted to the pool at increasing frequency
 * @param pool_size if 0: adaptive pool, otherwise: static
 */
void inc_load(int pool_size, int num_items, void *worker_function)
{
    int is[num_items];
    threadpool tpool = get_tpool(pool_size);
    debug_print("%s\n", "start submitting jobs to tpool");
    for (int i = 0; i < num_items; i++)
    {
        is[i] = i;
        tpool_submit_job(tpool, worker_function, &is[i]);
        if (i < num_items / 4)
            usleep(100000); // 100 ms
        else if (i < num_items / 2)
            usleep(30000); // 30ms
        else if (i < num_items / 4 * 3)
            usleep(10000); // 10ms
        else
            usleep(1000); // 1ms
    }
    tpool_wait_destroy(tpool);
    delete_files(num_items);
}

/**
 * a test run where background writers are started in fixed intervals
 * @param pool_size if 0: adaptive pool, otherwise: static
 */
void inc_background_load(int pool_size, int num_items, void *worker_function)
{
    int is[num_items];
    int background_id = 0;
    threadpool tpool = get_tpool(pool_size);

    debug_print("%s\n", "start submitting jobs to tpool");
    for (int i = 0; i < num_items; i++)
    {
        is[i] = i;
        tpool_submit_job(tpool, worker_function, &is[i]);
        // spawn a background writer after every completion of 1/8 of jobs
        // in total 7 background writers after completion of 7/8 jobs
        if (i == num_items / 8 || i == (num_items / 8) * 2 || i == (num_items / 8) * 3 || i == (num_items / 8) * 4 ||
            i == (num_items / 8) * 5 || i == (num_items / 8) * 6 || i == (num_items / 8) * 7)
        {
            tpool_submit_job(tpool, spawn_background_writer, &background_id);
            background_id += 1;
        }
    }

    tpool_wait_destroy(tpool);
    exiting = true;
    delete_files(num_items);
    // should be long enough for all background writers to
    sleep(1);
}

int main(int argc, char **argv)
{
    if (argc < 5)
    {
        printf("args: <worker_function> <test_name> <output_dir> <num_items> <pool_size> (just for static pool tests)\n");
        printf("valid worker function:\n");
        printf("--- worker_write_synced\n");
        printf("--- worker_read_buffered\n");
        printf("valid test names:\n");
        printf("--- adapt_pool-static_load\n");
        printf("--- adapt_pool-inc_load\n");
        printf("--- adapt_pool-inc_background_load\n");
        printf("--- adapt_pool-static_load-x2\n");
        printf("--- static_pool-static_load\n");
        printf("--- static_pool-inc_load\n");
        printf("--- static_pool-inc_background_load\n");
        printf("--- static_pool-static_load-x2\n");
        return -1;
    }
    else
    {
        int num_items = atoi(argv[4]);
        // PARSE OUTPUT DIRECTORY
        char *output_directory = argv[3];
        if (output_directory[strlen(output_directory) - 1] == '/')
        {
            output_directory[strlen(output_directory) - 1] = '\0';
        }
        OUTPUT_DIR = output_directory;
        // PARSE WORKER FUNCTION
        void *worker_function;
        if (strcmp(argv[1], "worker_write_synced") == 0)
        {
            worker_function = worker_write_synced;
        }
        else if (strcmp(argv[1], "worker_read_buffered") == 0)
        {
            worker_function = worker_read_buffered;
        }
        else
        {
            printf("invalid worker function\n");
            exit(1);
        }
        // PARSE TEST NAME
        if (strcmp(argv[2], "adapt_pool-static_load") == 0)
        {
            printf("%s\n", "RUNNING adaptive pool - static load");
            static_load(0, num_items, worker_function);
        }
        else if (strcmp(argv[2], "adapt_pool-inc_load") == 0)
        {
            printf("%s\n", "RUNNING adaptive pool - inc load");
            inc_load(0, num_items, worker_function);
        }
        else if (strcmp(argv[2], "adapt_pool-inc_background_load") == 0)
        {
            printf("%s\n", "RUNNING adaptive pool - inc background load");
            inc_background_load(0, num_items, worker_function);
        }
        else if (strcmp(argv[2], "static_pool-static_load") == 0)
        {
            int pool_size = atoi(argv[5]);
            printf("%s\n", "RUNNING static pool - static load");
            static_load(pool_size, num_items, worker_function);
        }
        else if (strcmp(argv[2], "static_pool-inc_load") == 0)
        {
            int pool_size = atoi(argv[5]);
            printf("%s\n", "RUNNING static pool - inc load");
            inc_load(pool_size, num_items, worker_function);
        }
        else if (strcmp(argv[2], "static_pool-inc_background_load") == 0)
        {
            int pool_size = atoi(argv[5]);
            printf("%s\n", "RUNNING static pool - inc background load");
            inc_background_load(pool_size, num_items, worker_function);
        }
        else if (strcmp(argv[2], "adapt_pool-static_load-x2") == 0)
        {
            pid_t fork_pid, wait_pid;
            int child_status = 0;
            printf("%s\n", "RUNNING 2x adaptive pool - static load in parallel");
            for (int i = 0; i < 2; i++)
            {
                fork_pid = fork();
                if (fork_pid > 0)
                {
                    static_load(0, num_items, worker_function);
                    exit(0);
                }
            }
            while ((wait_pid = wait(&child_status)) > 0)
                ;
        }
        else if (strcmp(argv[2], "static_pool-static_load-x2") == 0)
        {
            pid_t fork_pid, wait_pid;
            int child_status = 0;
            int pool_size = atoi(argv[5]);
            printf("%s\n", "RUNNING 2x static pool - static load in parallel");
            for (int i = 0; i < 2; i++)
            {
                fork_pid = fork();
                if (fork_pid > 0)
                {
                    static_load(pool_size, num_items, worker_function);
                    exit(0);
                }
            }
            while ((wait_pid = wait(&child_status)) > 0)
                ;
        }
        else
        {
            printf("invalid test name\n");
            exit(1);
        }
    }
    printf("done\n");
    return 0;
}
