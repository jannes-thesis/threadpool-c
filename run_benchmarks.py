import time
import sys
import json
from subprocess import run, DEVNULL, PIPE, STDOUT, Popen
from typing import Dict, Tuple, List


def run_static(benchmark_name: str, pool_size: int) -> float:
    start = time.perf_counter()
    run(['build/benchmark', benchmark_name, str(pool_size)],
        stderr=DEVNULL, stdout=DEVNULL)
    runtime_seconds = time.perf_counter() - start
    return runtime_seconds

def run_adaptive(benchmark_name: str) -> Tuple[float, List[str]]:
    start = time.perf_counter()
    logs= ''
    with Popen(['build/benchmark', benchmark_name, str(0)],
               text=True, stdout=PIPE, stderr=STDOUT) as proc:
        while proc.poll() is None:
            out, _ = proc.communicate()
            logs += out
    runtime_seconds = time.perf_counter() - start
    scale_lines = [line for line in logs.splitlines() if 'creator pid' in line]
    return runtime_seconds, scale_lines


if __name__ == '__main__':
    adaptive_benchmarks = ['adapt_pool-static_load', 'adapt_pool-inc_load', 'adapt_pool-static_load-x2']
    static_benchmarks = ['static_pool-static_load', 'static_pool-inc_load', 'static_pool-static_load-x2']
    static_sizes = [1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]
    output_path = sys.argv[1]

    result = {}
    # for benchmark in static_benchmarks:
    #     result[benchmark] = {}
    #     for size in static_sizes:
    #         run(['sudo', 'clear_buffer.sh'])
    #         print(f'running {benchmark} with size {size}')
    #         runtime_seconds = run_static(benchmark, size)
    #         print(f'took {runtime_seconds} seconds')
    #         result[benchmark][size] = runtime_seconds
    
    for benchmark in adaptive_benchmarks:
        run(['sudo', 'bash', 'clear_buffer.sh'])
        result[benchmark] = {}
        print(f'running {benchmark}')
        runtime_seconds, scale_lines = run_adaptive(benchmark)
        print(f'took {runtime_seconds} seconds')
        result[benchmark]['runtime'] = runtime_seconds
        result[benchmark]['scale_lines'] = scale_lines

    with open(output_path, 'w') as f:
        json.dump(result, f, indent=4)
