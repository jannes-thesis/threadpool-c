#ifndef scaling_adapter_h
#define scaling_adapter_h

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
  double scale_metric;
  double reset_metric;
} IntervalDerivedData;

typedef struct {
  uint32_t count;
  uint64_t total_time;
} SyscallData;

typedef struct {
  uint64_t start_ms;
  uint64_t end_ms;
  uint64_t read_bytes;
  uint64_t write_bytes;
  const SyscallData *syscalls_data;
  uintptr_t amount_targets;
} IntervalDataFFI;

typedef IntervalDerivedData (*CalcMetricsFunFFI)(const IntervalDataFFI*);

typedef struct {
  uint64_t check_interval_ms;
  const int32_t *syscall_nrs;
  uintptr_t amount_syscalls;
  CalcMetricsFunFFI calc_interval_metrics;
} AdapterParameters;

bool add_tracee(int32_t tracee_pid);

void close_adapter(void);

int32_t get_scaling_advice(void);

bool new_adapter(const AdapterParameters *parameters);

bool remove_tracee(int32_t tracee_pid);

#endif /* scaling_adapter_h */
