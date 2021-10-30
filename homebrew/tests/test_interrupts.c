// vim: set fileencoding=utf-8
#include <stdlib.h>
#include "naomi/interrupts.h"

void test_interrupts_basic(test_context_t *context)
{
    // TODO: This probably won't work once we have a more sophisticated interrupt model.
    irq_stats_t oldstats = irq_get_stats();

    // Generate a software interrupt.
    asm("trapa #00");

    irq_stats_t newstats = irq_get_stats();

    ASSERT(oldstats.num_interrupts + 1 == newstats.num_interrupts, "Didn't get exactly one interrupt!");
    ASSERT(newstats.last_source == IRQ_SOURCE_GENERAL_EXCEPTION, "Got unexpected interrupt source %lx!", newstats.last_source);
    ASSERT(newstats.last_event == IRQ_EVENT_TRAPA, "Got unexpected interrupt event %lx!", newstats.last_event);
}
