#ifndef __TIMER_H
#define __TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MAX_TIMERS 64

// Wait for the specified number of microseconds in a spin loop. This works both
// with interrupts enabled and disabled and has no restrictions on the number of
// microseconds as it handles both states internally.
void timer_wait(uint32_t microseconds);

// Start a timer that will expire after specified microseconds have elapsed.
// The return value is the handle which can be passed to timer_stop, timer_left
// and timer_elapsed. If we have run out of timer handles, then the function
// will return -1. It is still safe to pass this value to other timer functions,
// but they will all return 0. Much like timer_wait, this works both with interrupts
// enabled and disabled and has no restrictions on the number of microseconds
// requested.
int timer_start(uint32_t microseconds);

// Stop a previously started timer and clear all of its statuses.
void timer_stop(int timer);

// Return the number of microseconds left on an active timer.
uint32_t timer_left(int timer);

// Return the number of microseconds elapsed on an active timer.
uint32_t timer_elapsed(int timer);

#define MAX_PROFILERS 64

// Start a timer for the purpose of profiling code. This has granularity up to 1 second.
// If a negative value is returned, then profiling could not start. It is safe to pass
// a negative value into a profile_end() call.
int profile_start();

// Return the number of microseconds some code took, given the return from a profile start.
// If a negative value is passed into the profile option, 0 is returned as the elapsed time.
// Note that this is specifically designed to allow for an enormous amount of time to pass
// and still return accurate results. This is why the return value is a 64-bit integer.
uint64_t profile_end(int profile);

#ifdef __cplusplus
}
#endif

#endif
