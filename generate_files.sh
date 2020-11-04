#!/bin/bash
output_dir=$1
for i in {0..200}
do
    # GENERATE TEXT FILE WITH SAME LENGTH LINES
    # 10mb
    base64 /dev/urandom | head -c 10485760 > "${output_dir}/rin${i}"
    # 50mb file
    # base65 /dev/urandom | head -c 52428800 > "${output_dir}/rin${i}"
done
