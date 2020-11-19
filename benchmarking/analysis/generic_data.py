import json
from dataclasses import dataclass, field, asdict
from statistics import mean
from typing import *


@dataclass(frozen=True)
class BenchmarkParameters:
    workload: str
    thread_config: Mapping[str, int]


@dataclass(frozen=True)
class SyscallMetrics:
    name: str
    total_time_ms: float
    nr_calls: int
    avg_call_time_ms: float = field(init=False)

    def __post_init__(self):
        if self.nr_calls != 0:
            object.__setattr__(self, 'avg_call_time_ms',
                               self.total_time_ms / self.nr_calls)
        else:
            object.__setattr__(self, 'avg_call_time_ms', 0.0)


@dataclass(frozen=True)
class BenchmarkRun:
    workload: str
    thread_config: Mapping[str, int]
    runtime_s: float
    avg_latency_ms: float
    syscall_metrics: List[SyscallMetrics]
    # keys: write_bytes, read_bytes
    io_throughput: Mapping[str, float]
    iowait: int

    def as_dict(self) -> Dict:
        result = asdict(self)
        result['syscall_metrics'] = [
            asdict(sys_metric) for sys_metric in self.syscall_metrics]
        return result

    def get_params_str(self) -> str:
        result = self.workload
        for pool_name in self.thread_config.keys():
            result += f'-{pool_name}:{self.thread_config[pool_name]}'
        return result


def run_to_total_written_bytes_per_syswrite_ms(run: BenchmarkRun) -> float:
    written_bytes = run.io_throughput['write_bytes']
    total_write_time = [
        sm.total_time_ms for sm in run.syscall_metrics if sm.name == 'write'][0]
    return written_bytes / total_write_time


def runs_to_json_str(runs: List[BenchmarkRun]) -> str:
    dicts = [run.as_dict() for run in runs]
    return json.dumps(dicts, indent=4)


def json_to_runs(runs_arr: List) -> List[BenchmarkRun]:
    def syscall_metrics_from_dict(sm_dict):
        return SyscallMetrics(sm_dict['name'], float(sm_dict['total_time_ms']), int(sm_dict['nr_calls']))

    result = []
    for run in runs_arr:
        sms = [syscall_metrics_from_dict(sm) for sm in run['syscall_metrics']]
        result.append(BenchmarkRun(run['workload'], run['thread_config'], float(run['runtime_s']),
                                   float(run['avg_latency_ms']), sms, run['io_throughput'], run['iowait']))
    return result


def json_str_to_runs(runs_json: str) -> List[BenchmarkRun]:
    dict_arr = json.loads(runs_json)
    return json_to_runs(dict_arr)


def get_agg_function(agg_func_str: str):
    if agg_func_str == 'min':
        f = min
    elif agg_func_str == 'max':
        f = max
    elif agg_func_str == 'mean':
        f = mean
    else:
        raise Exception('invalid agg function')
    return f


def agg_syscall_metrics(syscall_metrics: List[SyscallMetrics], agg_func: str) -> List[SyscallMetrics]:
    f = get_agg_function(agg_func)
    name_metrics_map = {}
    for sm in syscall_metrics:
        if sm.name not in name_metrics_map:
            name_metrics_map[sm.name] = []
        name_metrics_map[sm.name].append((sm.total_time_ms, sm.nr_calls))
    result = []
    for name in name_metrics_map.keys():
        metric_tuples = name_metrics_map[name]
        mom_total_time = f([tuple[0] for tuple in metric_tuples])
        mom_nr_calls = f([tuple[1] for tuple in metric_tuples])
        result.append(SyscallMetrics(name, mom_total_time, mom_nr_calls))
    return result


def agg_io_throughputs(io_throughputs: List[Mapping[str, float]], agg_func: str) -> Mapping[str, float]:
    """
    :param io_throughputs:  list of maps with identical key sets
    :return:
    """
    f = get_agg_function(agg_func)
    keys = io_throughputs[0].keys()
    aggd_map = {}
    for key in keys:
        aggd = f([m[key] for m in io_throughputs])
        aggd_map[key] = aggd
    return aggd_map


def agg_runs(same_config_runs: List[BenchmarkRun], agg_func_str: str) -> BenchmarkRun:
    """
    :param same_config_runs: benchmark runs with identical parameters (workload, thread config)
    :return:
    """
    f = get_agg_function(agg_func_str)
    workload = same_config_runs[0].workload
    thread_config = same_config_runs[0].thread_config
    aggd_runtime = f([run.runtime_s for run in same_config_runs])
    aggd_latency = f([run.avg_latency_ms for run in same_config_runs])
    sms = []
    for run in same_config_runs:
        sms.extend(run.syscall_metrics)
    aggd_sysc_metrics = agg_syscall_metrics(sms, agg_func_str)
    aggd_io_throughputs = agg_io_throughputs(
        [run.io_throughput for run in same_config_runs], agg_func_str)
    aggd_iowait = f([run.iowait for run in same_config_runs])
    return BenchmarkRun(workload, thread_config, aggd_runtime, aggd_latency,
                        aggd_sysc_metrics, aggd_io_throughputs, aggd_iowait)


def agg_same_config_runs(runs: List[BenchmarkRun], agg_func_str: str) -> List[BenchmarkRun]:
    config_run_map = {}
    for run in runs:
        run_params = run.get_params_str()
        if run_params not in config_run_map:
            config_run_map[run_params] = []
        config_run_map[run_params].append(run)
    result = [agg_runs(runs, agg_func_str) for runs in config_run_map.values()]
    # sort so that when making graphs each workload is in order by thread count
    return sorted(result, key=lambda run: list(run.thread_config.values())[0])


def avg_same_config_runs(runs: List[BenchmarkRun]) -> List[BenchmarkRun]:
    return agg_same_config_runs(runs, 'mean')


def min_avg_max_same_config_runs(runs: List[BenchmarkRun]) -> List[Tuple[BenchmarkRun, BenchmarkRun, BenchmarkRun]]:
    avgd = agg_same_config_runs(runs, 'mean')
    mind = agg_same_config_runs(runs, 'min')
    maxd = agg_same_config_runs(runs, 'max')
    return list(zip(mind, avgd, maxd))


def get_params_set(runs: List[BenchmarkRun]) -> Tuple[Set[str], Mapping[str, Set[int]]]:
    """
    :return: set of workloads, mapping of pool names to set of sizes
    """
    workloads = set()
    pool_amounts = {}
    for run in runs:
        workloads.add(run.workload)
        for pool in run.thread_config:
            if pool not in pool_amounts:
                pool_amounts[pool] = set()
            pool_amounts[pool].add(run.thread_config[pool])
    return workloads, pool_amounts


def get_all_traced_syscall_names(runs: List[BenchmarkRun]) -> Set[str]:
    syscall_names = set()
    for run in runs:
        for sm in run.syscall_metrics:
            syscall_names.add(sm.name)
    return syscall_names


def get_always_traced_syscall_names(runs: List[BenchmarkRun]) -> Set[str]:
    syscall_names = {sm.name for sm in runs[0].syscall_metrics}
    for run in runs:
        syscall_names = syscall_names.intersection(
            {sm.name for sm in run.syscall_metrics})
    return syscall_names


def get_metric_or_zero(syscall_metrics: List[SyscallMetrics], syscall_name: str, metric_name: str) -> Union[float, int]:
    target_sm = [
        metrics for metrics in syscall_metrics if metrics.name == syscall_name]
    if len(target_sm) == 0:
        return 0.0
    else:
        return vars(target_sm[0])[metric_name]
