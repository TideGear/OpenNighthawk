#ifndef F117_GPU_TIMESTAMPS_H
#define F117_GPU_TIMESTAMPS_H
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
void *f117_timestamp_create(void *device, void *queue);
int f117_timestamp_mark(void *profiler, void *command_list, const char *label);
int f117_timestamp_resolve(void *profiler, void *command_list);
/* Caller must have waited for the final submission's existing fence. */
int f117_timestamp_read_after_fence(void *profiler, FILE *out);
void f117_timestamp_destroy(void *profiler);
#ifdef __cplusplus
}
#endif
#endif
