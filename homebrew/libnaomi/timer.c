#include <stdint.h>
#include <string.h>
#include "naomi/timer.h"
#include "naomi/interrupt.h"
#include "naomi/thread.h"
#include "irqstate.h"

#define TIMER_BASE_ADDRESS 0xFFD80000
#define MAX_HW_TIMERS 3

#define TOCR_OFFSET 0x00
#define TSTR_OFFSET 0x04
#define TCOR0_OFFSET 0x08
#define TCNT0_OFFSET 0x0C
#define TCR0_OFFSET 0x10
#define TCOR1_OFFSET 0x14
#define TCNT1_OFFSET 0x18
#define TCR1_OFFSET 0x1C
#define TCOR2_OFFSET 0x20
#define TCNT2_OFFSET 0x24
#define TCR2_OFFSET 0x28
#define TCPR2_OFFSET 0x2C

#define TIMER_TOCR *((volatile uint8_t *)(TIMER_BASE_ADDRESS + TOCR_OFFSET))
#define TIMER_TSTR *((volatile uint8_t *)(TIMER_BASE_ADDRESS + TSTR_OFFSET))

static unsigned int __tcor[MAX_HW_TIMERS] = {TCOR0_OFFSET, TCOR1_OFFSET, TCOR2_OFFSET};
#define TIMER_TCOR(x) *((volatile uint32_t *)(TIMER_BASE_ADDRESS + __tcor[x]))

static unsigned int __tcnt[MAX_HW_TIMERS] = {TCNT0_OFFSET, TCNT1_OFFSET, TCNT2_OFFSET};
#define TIMER_TCNT(x) *((volatile uint32_t *)(TIMER_BASE_ADDRESS + __tcnt[x]))

static unsigned int __tcr[MAX_HW_TIMERS] = {TCR0_OFFSET, TCR1_OFFSET, TCR2_OFFSET};
#define TIMER_TCR(x) *((volatile uint16_t *)(TIMER_BASE_ADDRESS + __tcr[x]))

#define TIMER_TCPR2 *((volatile uint32_t *)(TIMER_BASE_ADDRESS + TCPR2_OFFSET))

// A timer callback (will happen in interrupt context).
typedef int (*timer_callback_t)(int timer);

static uint32_t reset_values[MAX_HW_TIMERS];
static uint32_t timers_used[MAX_HW_TIMERS];
static timer_callback_t timer_callbacks[MAX_HW_TIMERS];

void _profile_init();
void _profile_free();
void _preempt_init();
void _preempt_free();
void _user_timer_init();
void _user_timer_free();
int _timer_available();
int _timer_start(int timer, uint32_t microseconds, timer_callback_t callback);
int _timer_stop(int timer);

void _timer_init()
{
    /* Disable all timers, set timers to internal clock source */
    TIMER_TSTR = 0;
    TIMER_TOCR = 0;

    for (int i = 0; i < MAX_HW_TIMERS; i++)
    {
        reset_values[i] = 0;
        timers_used[i] = 0;
        timer_callbacks[i] = 0;
    }

    // Schedule the profiler timer.
    _profile_init();

    // Schedule the periodic preemption timer.
    _preempt_init();

    // Initialize user timers.
    _user_timer_init();
}

void _timer_free()
{
    // Kill user timers.
    _user_timer_free();

    // Kill the periodic preemption timer.
    _preempt_free();

    // Kill the profiler.
    _profile_free();

    /* Disable all timers again */
    TIMER_TSTR = 0;

    for (int i = 0; i < MAX_HW_TIMERS; i++)
    {
        reset_values[i] = 0;
        timers_used[i] = 0;
        timer_callbacks[i] = 0;
    }
}

int _timer_interrupt(int timer)
{
    if (timer_callbacks[timer] != 0)
    {
        // Clear the underflow itself.
        TIMER_TCR(timer) &= ~0x100;

        // Call the callback.
        return timer_callbacks[timer](timer);
    }

    // Inform the scheduler that this was a regular callback.
    return 0;
}

static int preempt_timer = -1;

int _preempt_cb(int timer)
{
    // Inform the scheduler that this was a preemption request
    return -1;
}

void _preempt_init()
{
    // Make sure that we safely ask for a new timer.
    uint32_t old_interrupts = irq_disable();

    preempt_timer = _timer_available();
    if (preempt_timer >= 0 && preempt_timer < MAX_HW_TIMERS)
    {
        _timer_start(preempt_timer, MICROSECONDS_IN_ONE_SECOND / PREEMPTION_HZ, _preempt_cb);
    }

    // Enable interrupts again now that we're done.
    irq_restore(old_interrupts);
}

void _preempt_free()
{
    if (preempt_timer >= 0 && preempt_timer < MAX_HW_TIMERS)
    {
        _timer_stop(preempt_timer);
    }

    preempt_timer = -1;
}

int _timer_start(int timer, uint32_t microseconds, timer_callback_t callback)
{
    // Make sure we only ever check timers used without somebody else maybe calling us.
    uint32_t old_interrupts = irq_disable();

    /* Shouldn't be an issue but lets be cautious. */
    if (timer < 0 || timer >= MAX_HW_TIMERS || timers_used[timer])
    {
        // Couldn't checkout timer, this one is used or its an invalid handle.
        irq_restore(old_interrupts);
        return -1;
    }

    /* Calculate microsecond rate based on peripheral clock divided by 64.
     * According to online docs the DC and the Naomi have the same clock
     * speed so the calculation from KallistiOS should work here. */
    uint32_t rate = (uint32_t)((double)(50000000) * ((double)microseconds / (double)64000000));
    reset_values[timer] = microseconds;
    timers_used[timer] = 1;

    /* Count on peripheral clock / 64, no interrupt support for now */
    if (callback == 0)
    {
        TIMER_TCR(timer) = 0x2;
        timer_callbacks[timer] = 0;
    }
    else
    {
        TIMER_TCR(timer) = 0x22;
        timer_callbacks[timer] = callback;
    }

    /* Initialize the initial count */
    TIMER_TCNT(timer) = rate;
    TIMER_TCOR(timer) = rate;

    /* Start the timer */
    TIMER_TSTR |= (1 << timer);

    // Now that we've set everything up, re-enable interrupts.
    irq_restore(old_interrupts);

    return 0;
}

int _timer_stop(int timer)
{
    // Make sure to safely check timers used.
    uint32_t old_interrupts = irq_disable();

    /* Shouldn't be an issue but lets be cautious. */
    if (timer < 0 || timer >= MAX_HW_TIMERS || (!timers_used[timer])) {
        irq_restore(old_interrupts);
        return -1;
    }

    /* Stop the timer */
    TIMER_TSTR &= ~(1 << timer);

    /* Clear the underflow value */
    TIMER_TCR(timer) &= ~0x100;

    // Clear our bookkeeping.
    reset_values[timer] = 0;
    timers_used[timer] = 0;
    timer_callbacks[timer] = 0;

    // Safe to restore interrupts.
    irq_restore(old_interrupts);

    return 0;
}

uint32_t _timer_left(int timer)
{
    // Make sure to safely check timers used.
    uint32_t old_interrupts = irq_disable();

    /* Shouldn't be an issue but lets be cautious. */
    if (timer < 0 || timer >= MAX_HW_TIMERS || (!timers_used[timer])) {
        irq_restore(old_interrupts);
        return 0;
    }

    if (TIMER_TCR(timer) & 0x100) {
        /* Timer expired, no time left */
        irq_restore(old_interrupts);
        return 0;
    }

    /* Grab the value, convert back to microseconds. */
    uint32_t current = TIMER_TCNT(timer);

    // Safely restore interrupts and return the value.
    irq_restore(old_interrupts);
    return (uint32_t)(((double)current / (double)50000000) * (double)64000000);
}

uint32_t _timer_elapsed(int timer)
{
    // Make sure to safely check timers used.
    uint32_t old_interrupts = irq_disable();

    /* Shouldn't be an issue but lets be cautious. */
    if (timer < 0 || timer >= MAX_HW_TIMERS || (!timers_used[timer])) {
        irq_restore(old_interrupts);
        return 0;
    }

    // Grab elapsed time, restore interrupts.
    uint32_t elapsed = reset_values[timer] - _timer_left(timer);
    irq_restore(old_interrupts);

    return elapsed;
}

int _timer_available()
{
    int timer = -1;

    // Check timers under guard of interrupts so we know that a timer
    // is truly available.
    uint32_t old_interrupts = irq_disable();
    for (int i = 0; i < MAX_HW_TIMERS; i++)
    {
        if (!timers_used[i])
        {
            timer = i;
            break;
        }
    }
    irq_restore(old_interrupts);

    // Note that the timer we return here is still possibly stolen (race
    // condition possible), so it is best to do both a _timer_available()
    // and a _timer_start() request while interrupts are disabled. This is
    // what profiling code does below.
    return timer;
}

// Maximum number of microseconds a timer can be set to before an interrupt
// occurs to reset it. Higher numbers equal fewer interrupts but less accuracy.
#define MAX_PROFILE_MICROSECONDS MICROSECONDS_IN_ONE_SECOND

static uint64_t profile_timers[MAX_PROFILERS];
static uint64_t profile_current;
static int profile_timer = -1;

int _profile_cb(int timer)
{
    profile_current += MAX_PROFILE_MICROSECONDS;

    // Inform the scheduler that this was a regular callback.
    return 0;
}

void _profile_init()
{
    // Make sure that we safely ask for a new timer.
    uint32_t old_interrupts = irq_disable();

    memset(profile_timers, 0, sizeof(uint64_t) * MAX_PROFILERS);
    profile_current = 0;
    profile_timer = _timer_available();
    if (profile_timer >= 0 && profile_timer < MAX_HW_TIMERS)
    {
        _timer_start(profile_timer, MAX_PROFILE_MICROSECONDS, _profile_cb);
    }

    // Enable interrupts again now that we're done.
    irq_restore(old_interrupts);
}

void _profile_free()
{
    if (profile_timer >= 0 && profile_timer < MAX_HW_TIMERS)
    {
        _timer_stop(profile_timer);
    }

    memset(profile_timers, 0, sizeof(uint64_t) * MAX_PROFILERS);
    profile_current = 0;
    profile_timer = -1;
}

uint64_t _profile_get_current(uint32_t adjustments)
{
    uint32_t old_interrupts = irq_disable();
    uint64_t amount = 0;

    if (profile_timer >= 0 && profile_timer < MAX_HW_TIMERS)
    {
        amount = profile_current + _timer_elapsed(profile_timer) + (adjustments * MAX_PROFILE_MICROSECONDS);
    }

    irq_restore(old_interrupts);
    return amount;
}

int profile_start()
{
    uint32_t old_interrupts = irq_disable();
    int profile_slot = -1;

    if (profile_timer >= 0 && profile_timer < MAX_HW_TIMERS)
    {
        for (int slot = 0; slot < MAX_PROFILERS; slot++)
        {
            if (profile_timers[slot] == 0)
            {
                profile_timers[slot] = _profile_get_current(0);
                profile_slot = slot;
                break;
            }
        }
    }

    irq_restore(old_interrupts);
    return profile_slot;
}

uint64_t profile_end(int profile)
{
    uint32_t old_interrupts = irq_disable();
    uint64_t elapsed = 0;

    if (profile >= 0 && profile < MAX_PROFILERS && profile_timers[profile] != 0)
    {
        elapsed = _profile_get_current(0) - profile_timers[profile];
        profile_timers[profile] = 0;
    }

    // Safe to re-enable interrupts now.
    irq_restore(old_interrupts);
    return elapsed;
}

void timer_wait(uint32_t microseconds)
{
    // First, figure out if interrupts are disabled (have to manually increment
    // the profiler in this case).
    uint32_t old_interrupts = irq_disable();
    unsigned int irq_disabled = _irq_was_disabled(old_interrupts);
    int timer = profile_timer;
    irq_restore(old_interrupts);

    // Now, wait until the profile timer has passed our elapsed microseconds.
    if (timer >= 0 && timer < MAX_HW_TIMERS)
    {
        // We base this busyloop wait on the profile timer.
        uint64_t old_value = _profile_get_current(0);
        uint32_t adjustments = 0;

        while ((_profile_get_current(adjustments) - old_value) < microseconds)
        {
            if (irq_disabled && (TIMER_TCR(timer) & 0x100))
            {
                // This should not clear any pending interrupts for the timer, so
                // we don't adjust the profiler timer itself as it will make an
                // adjustment to its own counter inside the profile interrupt handler
                // callback.
                TIMER_TCR(timer) &= ~0x100;
                adjustments++;
            }
        }

        // Its possible that the profiler would miss an interrupt if we had interrupts
        // disabled and waited for a REALLY long time. So, fix that here.
        if (adjustments > 1)
        {
            profile_current += ((adjustments - 1) * MAX_PROFILE_MICROSECONDS);
        }
    }
}

typedef struct
{
    unsigned int handle;
    unsigned int irq_disabled;
    unsigned int adjustments;
    uint32_t microseconds;
    uint64_t profile_start;
} timer_t;

static timer_t timers[MAX_TIMERS];
static unsigned int timer_counter = MAX_TIMERS;

void _user_timer_init()
{
    // To make sure we never reuse a timer counter, but we can also use
    // the handle for any active timer to look up the timer itself with
    // a single array index. We will use timer_counter % MAX_TIMERS to
    // do that lookup but still compare it to the handle so that a timer
    // which was freed and another allocated at the same index does not
    // match.
    timer_counter = MAX_TIMERS;
    memset(timers, 0, sizeof(timer_t) * MAX_TIMERS);
}

void _user_timer_free()
{
    memset(timers, 0, sizeof(timer_t) * MAX_TIMERS);
}

int timer_start(uint32_t microseconds)
{
    uint32_t old_interrupts = irq_disable();
    unsigned int irq_disabled = _irq_was_disabled(old_interrupts);
    int timer = -1;

    for (unsigned int i = 0; i < MAX_TIMERS; i++)
    {
        unsigned int handle = timer_counter + i;
        unsigned int slot = handle % MAX_TIMERS;

        if (timers[slot].handle == 0)
        {
            // Found a timer! Rememer its full handle so we can compare later.
            timers[slot].handle = handle;
            timers[slot].profile_start = _profile_get_current(0);
            timers[slot].adjustments = 0;
            timers[slot].irq_disabled = irq_disabled;
            timers[slot].microseconds = microseconds;

            // Set our timer counter to this handle + 1 so that we find the next
            // slot on the next request for a new timer.
            timer = handle;
            timer_counter = handle + 1;

            // Get outta here! We got a timer.
            break;
        }
    }

    irq_restore(old_interrupts);
    return timer;
}

void timer_stop(int timer)
{
    uint32_t old_interrupts = irq_disable();

    if (timer >= 0)
    {
        unsigned int slot = timer % MAX_TIMERS;

        if (timers[slot].handle == timer)
        {
            // Found the previously allocated timer.
            timers[slot].handle = 0;
            timers[slot].profile_start = 0;
            timers[slot].adjustments = 0;
            timers[slot].irq_disabled = 0;
            timers[slot].microseconds = 0;
        }
    }

    irq_restore(old_interrupts);
}

#define CALCULATE_ELAPSED 0
#define CALCULATE_LEFT 1

uint32_t _timer_elapsed_or_left(int timer, int which)
{
    uint32_t old_interrupts = irq_disable();
    uint32_t calculated = 0;

    if (timer >= 0)
    {
        unsigned int slot = timer % MAX_TIMERS;
        if (timers[slot].handle == timer)
        {
            // Found the previously allocated timer.
            if (timers[slot].irq_disabled && (TIMER_TCR(timer) & 0x100))
            {
                // This should not clear any pending interrupts for the timer, so
                // we don't adjust the profiler timer itself as it will make an
                // adjustment to its own counter inside the profile interrupt handler
                // callback. Note, however, that if you wait a very long time (more
                // than a second) between two calls to timer_left or timer_elapsed
                // and you are running with interrupts disabled, the return values for
                // timer_left and timer_elapsed could be wrong (we don't know how many
                // adjustments to add here, since we don't know how many overflows occured).
                // It is not recommended to run for that amount of time without interrupts,
                // however, as many other things in the system will break, such as the
                // profile timer.
                TIMER_TCR(timer) &= ~0x100;
                timers[slot].adjustments++;
            }

            // Calculate actual delta.
            calculated = _profile_get_current(timers[slot].adjustments) - timers[slot].profile_start;
            if (calculated > timers[slot].microseconds)
            {
                calculated = timers[slot].microseconds;
            }

            // Flip it if we are requested to.
            if (which == CALCULATE_LEFT)
            {
                calculated = timers[slot].microseconds - calculated;
            }
        }
    }

    irq_restore(old_interrupts);
    return calculated;
}

uint32_t timer_left(int timer)
{
    return _timer_elapsed_or_left(timer, CALCULATE_LEFT);
}

uint32_t timer_elapsed(int timer)
{
    return _timer_elapsed_or_left(timer, CALCULATE_ELAPSED);
}
