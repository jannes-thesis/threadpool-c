cmake_minimum_required(VERSION 3.15)
project(threadpool C)
set(CMAKE_C_STANDARD 11)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

#add_executable(threadpool simple_static/simple_tpool.h simple_static/simple_tpool.c simple_static/test.c)
#add_executable(threadpool dynamic_proc/dyn_tpool.h dynamic_proc/dyn_tpool.c dynamic_proc/test.c dynamic_proc/task_stats.c dynamic_proc/task_stats.h)
add_executable(threadpool static_spinlock/spinlock_tpool.h static_spinlock/spinlock_tpool.c static_spinlock/test.c)

target_link_libraries(threadpool Threads::Threads)
