import time
import sys
import json
from subprocess import run, DEVNULL, PIPE, STDOUT, Popen
from typing import Dict, Tuple, List


def run_static(worker_function: str, benchmark_name: str, output_dir: str, num_items: int, pool_size: int) -> float:
    start = time.perf_counter()
    run(['build/benchmark', worker_function, benchmark_name, output_dir, str(num_items), str(pool_size)],
        stderr=DEVNULL, stdout=DEVNULL)
    runtime_seconds = time.perf_counter() - start
    return runtime_seconds


def run_adaptive(worker_function: str, benchmark_name: str, output_dir: str, num_items: int) -> Tuple[float, List[str]]:
    start = time.perf_counter()
    logs = ''
    with Popen(['build/benchmark', worker_function, benchmark_name, output_dir, str(num_items), str(0)],
               text=True, stdout=PIPE, stderr=STDOUT) as proc:
        while proc.poll() is None:
            out, _ = proc.communicate()
            logs += out
    runtime_seconds = time.perf_counter() - start
    scale_lines = [line for line in logs.splitlines() if 'creator pid' in line]
    return runtime_seconds, scale_lines


if __name__ == '__main__':
    adaptive_benchmarks = ['adapt_pool-static_load', 'adapt_pool-inc_load',
                           'adapt_pool-inc_background_load', 'adapt_pool-static_load-x2']
    static_benchmarks = ['static_pool-static_load', 'static_pool-inc_load',
                         'static_pool-inc_background_load', 'static_pool-static_load-x2']
    worker_functions = ['worker_write_synced', 'worker_read_buffered']
    # static_sizes = [1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]
    static_sizes = [1]
    # num_items = 200
    num_items = 20

    if len(sys.argv) != 3:
        print('args: <output_dir> <result_json_path>')
        sys.exit(1)

    output_dir = sys.argv[1]
    result_json_path = sys.argv[2]

    result: Dict = {}
    for worker_function in worker_functions:
        result[worker_function] = {}

        for benchmark in static_benchmarks:
            result[worker_function][benchmark] = {}
            for size in static_sizes:
                run(['sudo', 'bash', 'clear_page_cache.sh'])
                print(f'running {benchmark} with size {size}')
                runtime_seconds = run_static(
                    worker_function, benchmark, output_dir, num_items, size)
                print(f'took {runtime_seconds} seconds')
                result[worker_function][benchmark][size] = runtime_seconds

        # for benchmark in adaptive_benchmarks:
        #     run(['sudo', 'bash', 'clear_page_cache.sh'])
        #     result[worker_function][benchmark] = {}
        #     print(f'running {benchmark}')
        #     runtime_seconds, scale_lines = run_adaptive(
        #         worker_function, benchmark, output_dir, num_items)
        #     print(f'took {runtime_seconds} seconds')
        #     result[worker_function][benchmark]['runtime'] = runtime_seconds
        #     result[worker_function][benchmark]['scale_lines'] = scale_lines

    with open(result_json_path, 'w') as f:
        json.dump(result, f, indent=4)
