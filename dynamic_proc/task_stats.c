//
// Created by jannes on 23/04/2020.
//

#include "task_stats.h"

io_stats* get_task_io_stats(pid_t tid) {
    FILE* fp;
    char filename[128], line[256];
    io_stats* stat = malloc(sizeof(io_stats));

    sprintf(filename, "/proc/self/task/%u/io", tid);

    if ((fp = fopen(filename, "r")) == NULL) {
        free(stat);
        return NULL;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (!strncmp(line, "read_bytes:", 11)) {
            sscanf(line + 12, "%llu", &stat->read_bytes);
        } else if (!strncmp(line, "write_bytes:", 12)) {
            sscanf(line + 13, "%llu", &stat->write_bytes);
        }
    }

    fclose(fp);
    return stat;
}