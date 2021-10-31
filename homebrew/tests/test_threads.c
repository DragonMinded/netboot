// vim: set fileencoding=utf-8
#include <stdlib.h>
#include "naomi/thread.h"

void *thread_main(void *param)
{
    global_counter_increment(param);

    return (void *)thread_id();
}

void test_threads_basic(test_context_t *context)
{
    void *counter = global_counter_init(0);
    uint32_t thread = thread_create("test", thread_main, counter);

    ASSERT(thread != thread_id(), "Newly created thread has same ID as us?");

    thread_info_t info = thread_info(thread);

    ASSERT(strcmp(info.name, "test") == 0, "Newly created thread has invalid debug name!");
    ASSERT(info.priority == 0, "Newly created thread has wrong default priority!");
    ASSERT(info.alive == 1, "Newly created thread isn't alive!");
    ASSERT(info.running == 0, "Newly created thread is running already!");

    thread_start(thread);

    // TODO: Switch this to a join call.
    while ( 1 )
    {
        info = thread_info(thread);
        if (info.running == 0 && info.alive == 0)
        {
            break;
        }

        // Give the other thread some time to process.
        thread_yield();
    }

    ASSERT(global_counter_value(counter) == 1, "Thread did not increment global counter!");

    // Finally, give back the memory.
    thread_destroy(thread);
}
