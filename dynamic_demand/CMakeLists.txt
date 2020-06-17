cmake_minimum_required(VERSION 3.15)
project(threadpool C)
set(CMAKE_C_STANDARD 11)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(dynamic_demand spinlock_tpool.h spinlock_tpool.c test.c)

target_link_libraries(dynamic_demand Threads::Threads)
