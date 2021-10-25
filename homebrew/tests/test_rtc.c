#include <stdlib.h>
#include "naomi/rtc.h"

void test_rtc_get(test_context_t *context)
{
    uint32_t rtc_orig = rtc_get();
    uint32_t rtc_new;

    for (int i = 0; i < 4; i++)
    {
        // Wait a quarter of a second.
        timer_wait(250000);

        rtc_new = rtc_get();
        if (rtc_new != rtc_orig)
        {
            break;
        }
    }

    ASSERT(rtc_new == (rtc_orig + 1), "RTC counted up more than one second, %lu != %lu + 1!", rtc_new, rtc_orig);

    // Now, try waiting another second, to see that it counts up by one second.
    int waited = profile_start();
    while(rtc_get() == rtc_new) { ; }
    uint32_t amount = profile_end(waited);

    ASSERT(rtc_get() == (rtc_new + 1), "RTC did not count a second during a 1-second wait, %lu != %lu + 1!", rtc_get(), rtc_new);
    ASSERT(amount <= 1000000, "RTC did not count up within 1 second, instead took %lu us!", amount);
}

void test_rtc_set(test_context_t *context)
{
    // First, try setting it ahead 25 seconds.
    uint32_t rtc_orig = rtc_get();
    rtc_set(rtc_orig + 25);
    uint32_t rtc_new = rtc_get();

    ASSERT(rtc_new == rtc_orig + 25, "RTC was not updated, value is %lu instead of %lu!", rtc_new, rtc_orig + 25);

    // Now try setting it back.
    rtc_set(rtc_orig);
    uint32_t rtc_reset = rtc_get();

    ASSERT(rtc_reset == rtc_orig, "RTC was not rolled back, value is %lu instead of %lu!", rtc_reset, rtc_orig);
}
