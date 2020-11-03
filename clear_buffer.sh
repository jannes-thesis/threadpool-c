#!/bin/bash

# sync twice before dropping cache
/bin/sync
/bin/sync
/bin/echo 3 > /proc/sys/vm/drop_caches
