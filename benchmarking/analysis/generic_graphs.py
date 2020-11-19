from typing import *

from matplotlib.axes import Axes
from matplotlib.figure import Figure

from generic_data import BenchmarkRun, get_metric_or_zero


# if workload != None plot single workload, otherwise plot for all
def plot_generic_with_errors(workload: str, metric_y: str, metric_x: str,
                             runs_list: List[Tuple[BenchmarkRun, BenchmarkRun, BenchmarkRun]], ax: Axes):
    if workload is None:
        run_tuples = runs_list
        ax.set_title('all')
    else:
        run_tuples = [
            run_tuple for run_tuple in runs_list if run_tuple[0].workload == workload]
        ax.set_title(workload)
    xs = []
    avg_ys = []
    min_ys = []
    max_ys = []
    for run_tuple in run_tuples:
        if metric_x in run_tuple[0].thread_config:
            xs.append(run_tuple[0].thread_config[metric_x])
        elif metric_x in run_tuple[1].io_throughput:
            xs.append(run_tuple[1].io_throughput[metric_y])
        # if syscall metric: "<NAME-METRIC>"
        else:
            syscall_name, syscall_metric = metric_x.split('-')
            sm = [
                metrics for metrics in run_tuple[1].syscall_metrics if metrics.name == syscall_name][0]
            xs.append(vars(sm)[syscall_metric])
        # runtime or latency
        min_ys.append(vars(run_tuple[0])[metric_y])
        avg_ys.append(vars(run_tuple[1])[metric_y])
        max_ys.append(vars(run_tuple[2])[metric_y])
    y_upper_errors = [abs(pair[0] - pair[1]) for pair in zip(avg_ys, max_ys)]
    y_lower_errors = [abs(pair[0] - pair[1]) for pair in zip(avg_ys, min_ys)]
    ax.errorbar(xs, avg_ys, yerr=[y_lower_errors,
                                  y_upper_errors], color='tab:blue')
    ax.tick_params(axis='y', labelcolor='tab:blue')
    ax.set_xlabel(metric_x)
    ax.set_ylabel(metric_y)
    ax.grid(color='grey', linestyle='-', linewidth=0.25, alpha=0.5)


def plot_generic_extra_y_witherrors(
        workload: str, metric_y: str, metric_x: str,
        runs_list: List[Tuple[BenchmarkRun, BenchmarkRun, BenchmarkRun]],
        ax: Axes, fig: Figure, extra_y: str):
    """
    :param metric_x: must be threadcount metric
    :param metric_y: runtime/latency
    :param extra_y: must be syscall time metric
    :return:
    """
    plot_generic_with_errors(workload, metric_y, metric_x, runs_list, ax)
    if workload is None:
        run_tuples = runs_list
    else:
        run_tuples = [
            run_tuple for run_tuple in runs_list if run_tuple[0].workload == workload]
    xs = []
    avg_y2s = []
    min_y2s = []
    max_y2s = []
    for run_tuple in run_tuples:
        syscall_name, syscall_metric = extra_y.split('-')
        xs.append(run_tuple[0].thread_config[metric_x])
        min_y2s.append(get_metric_or_zero(
            run_tuple[0].syscall_metrics, syscall_name, syscall_metric))
        avg_y2s.append(get_metric_or_zero(
            run_tuple[1].syscall_metrics, syscall_name, syscall_metric))
        max_y2s.append(get_metric_or_zero(
            run_tuple[2].syscall_metrics, syscall_name, syscall_metric))
    y_upper_errors = [abs(pair[0] - pair[1]) for pair in zip(avg_y2s, max_y2s)]
    y_lower_errors = [abs(pair[0] - pair[1]) for pair in zip(avg_y2s, min_y2s)]
    ax2 = ax.twinx()  # instantiate a second axes that shares the same x-axis
    color = 'tab:orange'
    ax2.set_ylabel(extra_y, color=color)
    ax2.errorbar(xs, avg_y2s, yerr=[
                 y_lower_errors, y_upper_errors], color=color)
    ax2.tick_params(axis='y', labelcolor=color)
    fig.tight_layout()


def plot_generic_derived_y_errors(
        workload: str, metric_y: str, metric_x: str,
        runs_list: List[Tuple[BenchmarkRun, BenchmarkRun, BenchmarkRun]],
        ax: Axes, fig: Figure, derive_y_func: Callable[[BenchmarkRun], float], derived_y_name):
    """
    plot thread count vs runtime with second y axis of some derived metric (e.g throughput/syscalltime)
    :param metric_x: must be threadcount metric
    :param metric_y: runtime/latency
    :return:
    """
    plot_generic_with_errors(workload, metric_y, metric_x, runs_list, ax)
    if workload is None:
        run_tuples = runs_list
    else:
        run_tuples = [
            run_tuple for run_tuple in runs_list if run_tuple[0].workload == workload]
    xs = []
    avg_y2s = []
    min_y2s = []
    max_y2s = []
    for run_tuple in run_tuples:
        xs.append(run_tuple[0].thread_config[metric_x])
        min_y2s.append(derive_y_func(run_tuple[0]))
        avg_y2s.append(derive_y_func(run_tuple[1]))
        max_y2s.append(derive_y_func(run_tuple[2]))
    y_upper_errors = [abs(pair[0] - pair[1]) for pair in zip(avg_y2s, max_y2s)]
    y_lower_errors = [abs(pair[0] - pair[1]) for pair in zip(avg_y2s, min_y2s)]
    ax2 = ax.twinx()  # instantiate a second axes that shares the same x-axis
    color = 'tab:orange'
    ax2.set_ylabel(derived_y_name, color=color)
    ax2.errorbar(xs, avg_y2s, yerr=[
                 y_lower_errors, y_upper_errors], color=color)
    ax2.tick_params(axis='y', labelcolor=color)
    fig.tight_layout()
