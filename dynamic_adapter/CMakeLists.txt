add_executable(dynamic_adapter_benchmark adaptive_tpool.h adapter.h debug_macro.h adaptive_tpool.c benchmark.c)
target_link_libraries(dynamic_adapter ${CMAKE_SOURCE_DIR}/dynamic_adapter/adapter.a ${CMAKE_DL_LIBS} Threads::Threads)
set(CMAKE_BUILD_TYPE Debug)
