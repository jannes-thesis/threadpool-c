cmake_minimum_required(VERSION 3.15)
project(threadpool C)
set(CMAKE_C_STANDARD 11)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(dynamic_proc task_stats.h task_stats.c dyn_tpool.h dyn_tpool.c test.c)

target_link_libraries(dynamic_proc Threads::Threads)
