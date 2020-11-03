#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>
#include <string.h>

#include "debug_macro.h"
#include "adapter.h"
#include "adaptive_tpool.h"

static const size_t NUM_THREADS = 1;
static const size_t NUM_ITEMS = 1000;

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

void work_function(void *arg)
{
    int *valp = arg;
    debug_print("%s %d\n", "user function start", *valp);
    char filename[50];
    sprintf(filename, "wout%d", *valp);
    FILE *fp = fopen(filename, "w");
    int fd = fileno(fp);
    for (int i = 0; i < 1000; ++i)
    {
        fprintf(fp, "this is a line\n");
        fsync(fd);
    }
    debug_print("%s %d\n", "user function end", *valp);
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

void delete_files(char *output_prefix, int num_items)
{
    char filename[50];
    debug_print("%s\n", "deleting worker output");
    for (int j = 0; j < num_items; j++)
    {
        sprintf(filename, "%s%d.txt", output_prefix, j);
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
 * @param pool_size if 0: adaptive pool, otherwise: static
 */
void static_load(int pool_size, int num_items)
{
    int is[num_items];
    threadpool tpool = get_tpool(pool_size);

    debug_print("%s\n", "start submitting jobs to tpool");
    for (int i = 0; i < num_items; i++)
    {
        is[i] = i;
        tpool_submit_job(tpool, work_function, &is[i]);
    }

    tpool_wait_destroy(tpool);
    delete_files("wout", num_items);
}

/**
 * a test run where the items are submitted to the pool at increasing frequency
 * @param pool_size if 0: adaptive pool, otherwise: static
 * @param interval_ms
 * @param num_items
 */
void inc_load(int pool_size, int interval_ms, int num_items)
{
    int is[NUM_ITEMS];
    threadpool tpool = get_tpool(pool_size);
    debug_print("%s\n", "start submitting jobs to tpool");
    for (int i = 0; i < num_items; i++)
    {
        is[i] = i;
        tpool_submit_job(tpool, work_function, &is[i]);
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
    delete_files("wout", num_items);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("args: <test_name> <pool_size> (just for static pool tests)\n");
        printf("valid test names:\n");
        printf("--- adapt_pool-static_load\n");
        printf("--- adapt_pool-inc_load\n");
        printf("--- static_pool-static_load\n");
        printf("--- static_pool-inc_load\n");
        return -1;
    }
    else
    {
        if (strcmp(argv[1], "adapt_pool-static_load") == 0)
        {
            printf("%s\n", "RUNNING adaptive pool - static load");
            static_load(0, 200);
        }
        else if (strcmp(argv[1], "adapt_pool-inc_load") == 0)
        {
            printf("%s\n", "RUNNING adaptive pool - inc load");
            inc_load(0, 1000, 200);
        }
        else if (strcmp(argv[1], "static_pool-static_load") == 0)
        {
            int pool_size = atoi(argv[2]);
            printf("%s\n", "RUNNING static pool - static load");
            static_load(pool_size, 200);
        }
        else if (strcmp(argv[1], "static_pool-inc_load") == 0)
        {
            int pool_size = atoi(argv[2]);
            printf("%s\n", "RUNNING static pool - inc load");
            inc_load(pool_size, 1000, 200);
        }
    }
    printf("done\n");
    return 0;
}
