import os

import math
import sys
from matplotlib import pyplot as plt

from generic_data import *
from generic_graphs import *


def run_to_bytes_per_second(run: BenchmarkRun) -> float:
    written_bytes = run.io_throughput['write_bytes']
    read_bytes = run.io_throughput['read_bytes']
    rw_bytes = written_bytes + read_bytes
    return rw_bytes / run.runtime_s


def run_to_iowait_per_fsyncms(run: BenchmarkRun) -> float:
    fsync_total_ms = next(iter([m for m in run.syscall_metrics if m.name == 'fsync'])).total_time_ms
    return float(run.iowait) / fsync_total_ms


if __name__ == '__main__':
    result_json_path = sys.argv[1]
    output_dir = sys.argv[2]
    with open(result_json_path) as f:
        result_json = json.load(f)
    runs = json_to_runs(result_json)

    pool_name = 'num_io'
    # only use this script for results of single workload!
    workloads, pool_config_map = get_params_set(runs)
    workload = list(workloads)[0]

    thread_amounts = pool_config_map[pool_name]
    syscall_names = get_always_traced_syscall_names(runs)

    runs_min_avg_max = min_avg_max_same_config_runs(runs)
    subdir = 'graphs_runtime-avgsysc'
    os.makedirs(f'{output_dir}/{subdir}')
    for syscall in syscall_names:
        fig, ax = plt.subplots(figsize=(15, 10))
        plot_generic_extra_y_witherrors(workload, 'runtime_s', pool_name,
                                            runs_min_avg_max, ax, fig,
                                            f'{syscall}-avg_call_time_ms')
        fig.savefig(
            f'{output_dir}/{subdir}/runtime-{pool_name}-{syscall}_errors.png')
        plt.close(fig)

    subdir = 'graphs_runtime-totalsysc'
    os.makedirs(f'{output_dir}/{subdir}')
    for syscall in syscall_names:
        fig, ax = plt.subplots(figsize=(15, 10))
        plot_generic_extra_y_witherrors(workload, 'runtime_s', pool_name,
                                            runs_min_avg_max, ax, fig,
                                            f'{syscall}-total_time_ms')
        fig.savefig(
            f'{output_dir}/{subdir}/runtime-{pool_name}-{syscall}_errors.png')
        plt.close(fig)

    subdir = 'graphs_runtime-derived'
    os.makedirs(f'{output_dir}/{subdir}')

    # write bytes per write syscall ms
    fig, ax = plt.subplots(figsize=(15, 10))
    plot_generic_derived_y_errors(workload, 'runtime_s', pool_name,
                                      runs_min_avg_max, ax, fig,
                                      run_to_total_written_bytes_per_syswrite_ms,
                                      'written bytes / write syscall ms')
    fig.savefig(
        f'{output_dir}/{subdir}/runtime-{pool_name}-throughput-perwrite_bytes-ms.png')
    plt.close(fig)

    # write bytes per second
    fig, ax = plt.subplots(figsize=(15, 10))
    plot_generic_derived_y_errors(workload, 'runtime_s', pool_name,
                                      runs_min_avg_max, ax, fig,
                                      run_to_bytes_per_second,
                                      'written bytes / second')
    fig.savefig(
        f'{output_dir}/{subdir}/runtime-{pool_name}-throughput-bytes_sec.png')
    plt.close(fig)

    # total iowait ticks
    fig, ax = plt.subplots(figsize=(15, 10))
    plot_generic_derived_y_errors(workload, 'runtime_s', pool_name,
                                      runs_min_avg_max, ax, fig,
                                      lambda run: run.iowait,
                                      'total iowait')
    fig.savefig(
        f'{output_dir}/{subdir}/runtime-{pool_name}-iowait.png')
    plt.close(fig)

    # iowait ticks per fsync ms
    fig, ax = plt.subplots(figsize=(15, 10))
    plot_generic_derived_y_errors(workload, 'runtime_s', pool_name,
                                      runs_min_avg_max, ax, fig,
                                      lambda run: run_to_iowait_per_fsyncms(run),
                                      'iowait/fsync')
    fig.savefig(
        f'{output_dir}/{subdir}/runtime-{pool_name}-iowait-fsync-ratio.png')
    plt.close(fig)