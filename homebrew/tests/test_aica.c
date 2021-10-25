#include <stdlib.h>
#include <math.h>
#include "naomi/audio.h"
#include "naomi/timer.h"

void test_aica_simple(test_context_t *context)
{
    extern uint8_t *aica_test_bin_data;
    extern unsigned int aica_test_bin_len;

    ASSERT((((uint32_t )aica_test_bin_data) & 0x3) == 0, "AICA compiled binary is misaligned!");
    ASSERT((aica_test_bin_len & 0x3) == 0, "AICA compiled binary has invalid size %d!", aica_test_bin_len);

    load_aica_binary(aica_test_bin_data, aica_test_bin_len);

    volatile uint32_t * status_location = (volatile uint32_t *)((SOUNDRAM_BASE | UNCACHED_MIRROR) + 0xF100);
    int got_result = 0;

    // Wait at most 1ms before giving up.
    int timer = timer_available();
    timer_start(timer, 1000);
    while (timer_left(timer) > 0 && !got_result)
    {
        if ((*status_location) == 0xCAFEBABE)
        {
            got_result = 1;
        }
    }

    // Return the timer.
    timer_stop(timer);

    // Make sure we got the right response.
    ASSERT(got_result, "Failed to get acknowledgement from AICA test binary, %08lx != %08x!", *status_location, 0xCAFEBABE);
    ASSERT((*status_location) == 0xCAFEBABE, "Failed to read correct value from AICA test binary, %08lx != %08x!", *status_location, 0xCAFEBABE);
}
