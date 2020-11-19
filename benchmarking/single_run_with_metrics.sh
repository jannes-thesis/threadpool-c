#!/bin/bash
worker_f=$1
test_name=$2
out_dir=$3
num_items=$4
type=$5
size_or_params=$6
output_prefix=$7

sudo -v
start_millis=`date +%s%3N`
nohup ../build/benchmark $worker_f $test_name $out_dir $num_items $type $size_or_params > /dev/null 2> /dev/null < /dev/null &
staprun_pid=$!
main_pid=$!
echo "benchmark exe pid: ${main_pid}"
sleep 3
# get threads of workers
worker_tids=$(./get_child_tids.sh benchmark worker-)
echo "tokio workers: ${worker_tids}"

set -m
sudo nohup staprun topsysm2.ko "targets_arg=$worker_tids" -o "${output_prefix}-syscalls.txt" > /dev/null 2> /dev/null < /dev/null &
staprun_pid=$!
echo "staprun pid for workers: ${staprun_pid}"
pidstat_lite $main_pid $worker_tids > "${output_prefix}-pidstats.txt" &
pidstat_pid=$!
echo "pidstat pid: ${pidstat_pid}"

wait $main_pid 
end_millis=`date +%s%3N`
sudo kill -INT $staprun_pid
tail --pid=$staprun_pid -f /dev/null
# make sure staprun result file is written to disk
sync
let runtime=$end_millis-$start_millis
echo $runtime > "${output_prefix}-runtime_ms.txt"
