// vim: set fileencoding=utf-8
#include <stdlib.h>
#include "naomi/thread.h"

void *thread_main(void *param)
{
    global_counter_increment(param);

    return (void *)thread_id() + 1000;
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

    // Start the thread, wait until its done.
    thread_start(thread);
    uint32_t expected_id = (uint32_t)thread_join(thread);

    ASSERT(global_counter_value(counter) == 1, "Thread did not increment global counter!");
    ASSERT(expected_id == (thread + 1000), "Thread did not return correct value!");

    // Finally, give back the memory.
    thread_destroy(thread);
}
