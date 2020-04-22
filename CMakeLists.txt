cmake_minimum_required(VERSION 3.15)
project(ThreadingC C)

set(CMAKE_C_STANDARD 99)

add_executable(ThreadingC simple_tpool.h simple_tpool.c test.c)