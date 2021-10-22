#include <stdint.h>
#include "naomi/timer.h"

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

void timer_init()
{
    /* Disable all timers, set timers to internal clock source */
    TIMER_TSTR = 0;
    TIMER_TOCR = 0;

    for (int i = 0; i < MAX_TIMERS; i++)
    {
        reset_values[i] = 0;
        timers_used[i] = 0;
    }
}

void timer_free()
{
    /* Disable all timers again */
    TIMER_TSTR = 0;

    for (int i = 0; i < MAX_TIMERS; i++)
    {
        reset_values[i] = 0;
        timers_used[i] = 0;
    }
}

void timer_start(int timer, uint32_t microseconds)
{
    /* Shouldn't be an issue but lets be cautious. */
    if (timer < 0 || timer >= MAX_TIMERS || timers_used[timer]) {
        return;
    }

    /* Calculate microsecond rate based on peripheral clock divided by 64.
     * According to online docs the DC and the Naomi have the same clock
     * speed so the calculation from KallistiOS should work here. */
    uint32_t rate = (uint32_t)((double)(50000000) * ((double)microseconds / (double)64000000));
    reset_values[timer] = microseconds;
    timers_used[timer] = 1;

    /* Count on peripheral clock / 64, no interrupt support for now */
    TIMER_TCR(timer) = 0x2;

    /* Initialize both the initial count and the reset count */
    TIMER_TCNT(timer) = rate;
    TIMER_TCOR(timer) = rate;

    /* Start the timer */
    TIMER_TSTR |= (1 << timer);
}

void timer_stop(int timer)
{
    /* Shouldn't be an issue but lets be cautious. */
    if (timer < 0 || timer >= MAX_TIMERS || (!timers_used[timer])) {
        return;
    }

    reset_values[timer] = 0;
    timers_used[timer] = 0;

    /* Stop the timer */
    TIMER_TSTR &= ~(1 << timer);

    /* Clear the underflow value */
    TIMER_TCR(timer) &= ~0x100;
}

uint32_t timer_left(int timer)
{
    /* Shouldn't be an issue but lets be cautious. */
    if (timer < 0 || timer >= MAX_TIMERS || (!timers_used[timer])) {
        return 0;
    }

    if (TIMER_TCR(timer) & 0x100) {
        /* Timer expired, no time left */
        return 0;
    }

    /* Grab the value, convert back to microseconds. */
    uint32_t current = TIMER_TCNT(timer);
    return (uint32_t)(((double)current / (double)50000000) * (double)64000000);
}

uint32_t timer_elapsed(int timer)
{
    /* Shouldn't be an issue but lets be cautious. */
    if (timer < 0 || timer >= MAX_TIMERS || (!timers_used[timer])) {
        return 0;
    }

    return reset_values[timer] - timer_left(timer);
}

int profile_start()
{
    for (int i = 0; i < MAX_TIMERS; i++)
    {
        if (!timers_used[i]) {
            timer_start(i, 1000000);
            return i;
        }
    }

    return -1;
}

uint32_t profile_end(int profile)
{
    if (profile < 0 || profile >= MAX_TIMERS) {
        /* Couldn't init profile */
        return 0;
    }

    uint32_t elapsed = timer_elapsed(profile);
    timer_stop(profile);

    return elapsed;
}

void timer_wait(uint32_t microseconds)
{
    for (int i = 0; i < MAX_TIMERS; i++)
    {
        if (!timers_used[i]) {
            timer_start(i, microseconds);
            while ((TIMER_TCR(i) & 0x100) == 0) { ; }
            timer_stop(i);
        }
    }
}
