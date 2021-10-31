#ifndef __THREADS_H
#define __THREADS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef void (*thread_func_t)(void *param);

// Thread-safe global counters. Guaranteed to be atomically incremented/decremented
// as well as never go below zero. Also guaranteed to be safe to call increment/decrement/value
// on a counter that was freed in another thread.
#define MAX_GLOBAL_COUNTERS 64

void *global_counter_init(uint32_t initial_value);
void global_counter_increment(void *counter);
void global_counter_decrement(void *counter);
uint32_t global_counter_value(void *counter);
void global_counter_free(void *counter);

#ifdef __cplusplus
}
#endif

#endif
