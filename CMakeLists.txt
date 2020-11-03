cmake_minimum_required(VERSION 3.15)
project(adaptive_threadpool C)
set(CMAKE_C_STANDARD 11)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(benchmark adaptive_tpool.h adapter.h debug_macro.h adaptive_tpool.c benchmark.c)
target_link_libraries(benchmark ${CMAKE_SOURCE_DIR}/adapter.a ${CMAKE_DL_LIBS} Threads::Threads)
set(CMAKE_BUILD_TYPE Debug)
