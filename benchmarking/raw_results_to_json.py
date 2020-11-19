import json
from run_benchmark_traced import BenchmarkParameters, get_bench_params


def process_systemtap_output(lines):
    syscall_map = {}
    for line in lines[2:-1]:
        syscall, time, count = line.split()
        syscall_map[syscall] = (float(time), float(
            count), float(time) / float(count))
    return syscall_map


def convert_pidstat_line(line):
    fields = line.split()
    return int(fields[0]), int(fields[1]), int(fields[2])


def convert_pidstat(lines):
    """
    return aggregated bytes read/written
    """
    worker_lines = lines[3:]
    lines_fields = [convert_pidstat_line(line) for line in worker_lines]
    agg_read = sum([fields[0] for fields in lines_fields])
    agg_write = sum([fields[1] for fields in lines_fields])
    io_wait = sum([fields[2] for fields in lines_fields])
    return agg_read, agg_write, io_wait


def process_result_files(result_dir, thread_amounts, benchmark_name):
    results = []
    for amount in thread_amounts:
        config_prefix = f'{result_dir}/t={str(amount)}'
        systemtap_file = f'{config_prefix}-syscalls.txt'
        pidstats_file = f'{config_prefix}-pidstats.txt'
        result_file = f'{config_prefix}-runtime_ms.txt'
        benchmark_run = {'workload': benchmark_name, 'thread_config': {'num_io': amount} }
        with open(result_file) as f:
            lines = f.readlines()
            runtime_ms = float(lines[-1])
            benchmark_run['runtime_s'] = runtime_ms / 1000
            benchmark_run['avg_latency_ms'] = -1
        with open(systemtap_file) as f:
            lines = f.readlines()
            stats = process_systemtap_output(lines)
            stats_maps = [{'name': key, 'total_time_ms': stats[key][0], 'nr_calls': stats[key][1],
                           'avg_call_time_ms': stats[key][2]} for key in stats.keys()]
            benchmark_run['syscall_metrics'] = stats_maps
        with open(pidstats_file) as f:
            lines = f.readlines()
            bytes_read, bytes_written, iowait = convert_pidstat(lines)
            benchmark_run['io_throughput'] = {
                'read_bytes': bytes_read, 'write_bytes': bytes_written}
            benchmark_run['iowait'] = iowait
        results.append(benchmark_run)
    return results


if __name__ == '__main__':
    import sys
    if len(sys.argv) != 4:
        print('usage: ./raw_results_to_json.py [combined result dir] [number reps] [benchmark_name]')
        exit(1)

    result_base = sys.argv[1]
    n = int(sys.argv[2])
    benchmark_name = sys.argv[3]

    b_params = get_bench_params(benchmark_name)
    thread_amounts = b_params.io_threads
    runs_list = []

    if n == 1:
        results = process_result_files(result_base, thread_amounts, benchmark_name)
        runs_list.extend(results)
    else:
        for i in range(1, n + 1):
            result_dir = result_base + f'/{i}'
            results = process_result_files(result_dir, thread_amounts, benchmark_name)
            runs_list.extend(results)
    with open(f'{result_base}/result-all-{benchmark_name}.json', 'w') as f:
        json.dump(runs_list, f, indent=4)
