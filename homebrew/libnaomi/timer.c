#include <stdint.h>
#include "naomi/timer.h"
#include "naomi/interrupt.h"

#define TIMER_BASE_ADDRESS 0xFFD80000

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

static unsigned int __tcor[MAX_TIMERS] = {TCOR0_OFFSET, TCOR1_OFFSET, TCOR2_OFFSET};
#define TIMER_TCOR(x) *((volatile uint32_t *)(TIMER_BASE_ADDRESS + __tcor[x]))

static unsigned int __tcnt[MAX_TIMERS] = {TCNT0_OFFSET, TCNT1_OFFSET, TCNT2_OFFSET};
#define TIMER_TCNT(x) *((volatile uint32_t *)(TIMER_BASE_ADDRESS + __tcnt[x]))

static unsigned int __tcr[MAX_TIMERS] = {TCR0_OFFSET, TCR1_OFFSET, TCR2_OFFSET};
#define TIMER_TCR(x) *((volatile uint16_t *)(TIMER_BASE_ADDRESS + __tcr[x]))

#define TIMER_TCPR2 *((volatile uint32_t *)(TIMER_BASE_ADDRESS + TCPR2_OFFSET))

static uint32_t reset_values[MAX_TIMERS];
static uint32_t timers_used[MAX_TIMERS];
static timer_callback_t timer_callbacks[MAX_TIMERS];

void _timer_init()
{
    /* Disable all timers, set timers to internal clock source */
    TIMER_TSTR = 0;
    TIMER_TOCR = 0;

    for (int i = 0; i < MAX_TIMERS; i++)
    {
        reset_values[i] = 0;
        timers_used[i] = 0;
        timer_callbacks[i] = 0;
    }
}

void _timer_free()
{
    /* Disable all timers again */
    TIMER_TSTR = 0;

    for (int i = 0; i < MAX_TIMERS; i++)
    {
        reset_values[i] = 0;
        timers_used[i] = 0;
        timer_callbacks[i] = 0;
    }
}

void _timer_interrupt(int timer)
{
    if (timer_callbacks[timer] != 0)
    {
        // Clear the underflow itself.
        TIMER_TCR(timer) &= ~0x100;

        // Call the callback.
        timer_callbacks[timer](timer);
    }
}

int timer_start(int timer, uint32_t microseconds, timer_callback_t callback)
{
    // Make sure we only ever check timers used without somebody else maybe calling us.
    uint32_t old_interrupts = irq_disable();

    /* Shouldn't be an issue but lets be cautious. */
    if (timer < 0 || timer >= MAX_TIMERS || timers_used[timer])
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

int timer_stop(int timer)
{
    // Make sure to safely check timers used.
    uint32_t old_interrupts = irq_disable();

    /* Shouldn't be an issue but lets be cautious. */
    if (timer < 0 || timer >= MAX_TIMERS || (!timers_used[timer])) {
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

uint32_t timer_left(int timer)
{
    // Make sure to safely check timers used.
    uint32_t old_interrupts = irq_disable();

    /* Shouldn't be an issue but lets be cautious. */
    if (timer < 0 || timer >= MAX_TIMERS || (!timers_used[timer])) {
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

uint32_t timer_elapsed(int timer)
{
    // Make sure to safely check timers used.
    uint32_t old_interrupts = irq_disable();

    /* Shouldn't be an issue but lets be cautious. */
    if (timer < 0 || timer >= MAX_TIMERS || (!timers_used[timer])) {
        irq_restore(old_interrupts);
        return 0;
    }

    // Grab elapsed time, restore interrupts.
    uint32_t elapsed = reset_values[timer] - timer_left(timer);
    irq_restore(old_interrupts);

    return elapsed;
}

int timer_available()
{
    int timer = -1;

    // Check timers under guard of interrupts so we know that a timer
    // is truly available.
    uint32_t old_interrupts = irq_disable();
    for (int i = 0; i < MAX_TIMERS; i++)
    {
        if (!timers_used[i])
        {
            timer = i;
            break;
        }
    }
    irq_restore(old_interrupts);

    // Note that the timer we return here is still possibly stolen (race
    // condition possible), so it is best to do both a timer_available()
    // and a timer_start() request while interrupts are disabled. This is
    // what profiling code does below.
    return timer;
}

// Maximum number of microseconds a timer can be set to before an interrupt
// occurs to reset it. Higher numbers equal fewer interrupts but less accuracy.
#define MAX_PROFILE_MICROSECONDS 1000000

static uint32_t profile_rollovers[MAX_TIMERS];

void __profile_cb(int timer)
{
    profile_rollovers[timer] ++;
}

int profile_start()
{
    // Make sure that we safely ask for a new timer.
    uint32_t old_interrupts = irq_disable();

    int timer = timer_available();
    if (timer >= 0 && timer < MAX_TIMERS)
    {
        profile_rollovers[timer] = 0;
        timer_start(timer, MAX_PROFILE_MICROSECONDS, __profile_cb);
    }

    // Enable interrupts again now that we're done.
    irq_restore(old_interrupts);

    return timer;
}

uint32_t profile_end(int profile)
{
    if (profile < 0 || profile >= MAX_TIMERS) {
        /* Couldn't init profile */
        return 0;
    }

    // Make sure to disable interrupts so we can safely read the rollovers.
    uint32_t old_interrupts = irq_disable();

    // Grab the current timer state, add in the number of rollovers as the max microseconds.
    uint32_t elapsed = timer_elapsed(profile);
    elapsed += MAX_PROFILE_MICROSECONDS * profile_rollovers[profile];

    // Stop the timer, we don't need it anymore.
    timer_stop(profile);

    // Safe to re-enable interrupts now.
    irq_restore(old_interrupts);

    // Return final calculated value.
    return elapsed;
}

int timer_wait(uint32_t microseconds)
{
    // Disable interrupts so we are safe to grab a new timer.
    uint32_t old_interrupts = irq_disable();

    // Start a timer if we found one.
    int timer = timer_available();
    if (timer >= 0 && timer < MAX_TIMERS)
    {
        timer_start(timer, microseconds, 0);
    }

    // Safe to re-enable interrupts now.
    irq_restore(old_interrupts);

    // Wait until we have no time left.
    while (timer_left(timer) > 0) { ; }

    // Now, stop the timer (safe to do outside of interrupts as
    // timer_stop() is resilient to dummy timer values.
    timer_stop(timer);

    // Finally, return whether the wait was successful.
    return (timer >= 0 && timer < MAX_TIMERS) ? 0 : -1;
}
