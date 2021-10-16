#ifndef __TIMER_H
#define __TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define TIMER_BASE_ADDRESS 0xFFD80000
#define MAX_TIMERS 3

// You do not have to call these functions as they are handled for you
// by the runtime.
void timer_init();
void timer_free();

// Wait for the specified number of microseconds in a spin loop.
void timer_wait(uint32_t microseconds);

// Start a timer that will expire after specified microseconds have elapsed.
void timer_start(int timer, uint32_t microseconds);

// Stop a previously started timer and clear all of its statuses.
void timer_stop(int timer);

// Return the number of microseconds left on an active timer.
uint32_t timer_left(int timer);

// Return the number of microseconds elapsed on an active timer.
uint32_t timer_elapsed(int timer);

// Start a timer for the purpose of profiling code. This has granularity up to 1 second.
int profile_start();

// Return the number of microseconds some code took, given the return from a profile start.
uint32_t profile_end(int profile);

#ifdef __cplusplus
}
#endif

#endif
