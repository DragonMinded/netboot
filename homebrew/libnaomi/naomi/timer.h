#ifndef __TIMER_H
#define __TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MAX_TIMERS 3

// Wait for the specified number of microseconds in a spin loop. If the function
// returns nonzero, then the wait could not be performed. If the function returns
// zero, then at least the specified microseconds have elapsed.
int timer_wait(uint32_t microseconds);

// Grab the next available timer handle.
int timer_available();

// A timer callback (will happen in interrupt context).
typedef void (*timer_callback_t)(int timer);

// Start a timer that will expire after specified microseconds have elapsed.
// If callback is supplied, it will be called every "microseconds" period.
// Otherwise, a one-shot timer is created and timer_left() and timer_elapsed()
// can be called to ask about time left/time elapsed. Returns zero if a timer
// was successfully started, nonzero otherwise. If nonzero is returned, then
// the calling code should not call any other functions including timer_stop().
int timer_start(int timer, uint32_t microseconds, timer_callback_t callback);

// Stop a previously started timer and clear all of its statuses. Returns zero
// on success and nonzero on bad timer input.
int timer_stop(int timer);

// Return the number of microseconds left on an active timer.
uint32_t timer_left(int timer);

// Return the number of microseconds elapsed on an active timer.
uint32_t timer_elapsed(int timer);

// Start a timer for the purpose of profiling code. This has granularity up to 1 second.
// If a negative value is returned, then profiling could not start. It is safe to pass
// a negative value into a profile_end() call.
int profile_start();

// Return the number of microseconds some code took, given the return from a profile start.
// If a negative value is passed into the profile option, 0 is returned as the elapsed time.
uint32_t profile_end(int profile);

#ifdef __cplusplus
}
#endif

#endif
