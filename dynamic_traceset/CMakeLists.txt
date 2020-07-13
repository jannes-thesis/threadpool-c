add_subdirectory(lib_traceset)
add_executable(dynamic_traceset adaptive_tpool.h adaptive_tpool.c scaling.h scaling.c test.c)
target_link_libraries(dynamic_traceset Threads::Threads lib_traceset)
