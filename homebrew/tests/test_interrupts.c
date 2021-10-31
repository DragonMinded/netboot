// vim: set fileencoding=utf-8
#include <stdlib.h>
#include "naomi/interrupt.h"
#include "naomi/thread.h"

void test_interrupts_basic(test_context_t *context)
{
    irq_stats_t oldstats = irq_get_stats();

    // Use a simple syscall-based counter which will generate interrupts.
    void *counter = global_counter_init(1337);

    ASSERT(global_counter_value(counter) == 1337, "Got wrong value back from counter!");

    global_counter_increment(counter);
    irq_stats_t newstats = irq_get_stats();

    ASSERT(oldstats.num_interrupts < newstats.num_interrupts, "Didn't get any interrupts!");
    ASSERT(global_counter_value(counter) == 1338, "Got wrong value back from counter!");

    global_counter_decrement(counter);
    oldstats = newstats;
    newstats = irq_get_stats();

    ASSERT(oldstats.num_interrupts < newstats.num_interrupts, "Didn't get any interrupts!");
    ASSERT(global_counter_value(counter) == 1337, "Got wrong value back from counter!");

    global_counter_free(counter);

    ASSERT(global_counter_value(counter) == 0, "Got wrong value back from counter!");

    oldstats = newstats;
    newstats = irq_get_stats();
    ASSERT(oldstats.num_interrupts < newstats.num_interrupts, "Didn't get any interrupts!");
}
