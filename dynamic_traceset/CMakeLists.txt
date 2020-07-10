add_executable(dynamic_traceset spinlock_tpool.h spinlock_tpool.c test.c)
target_link_libraries(dynamic_traceset Threads::Threads)
