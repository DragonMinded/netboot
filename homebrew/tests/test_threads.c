// vim: set fileencoding=utf-8
#include <stdlib.h>
#include <math.h>
#include "naomi/thread.h"

void *basic_thread(void *param)
{
    global_counter_increment(param);

    return (void *)thread_id() + 1000;
}

void test_threads_basic(test_context_t *context)
{
    void *counter = global_counter_init(0);
    uint32_t thread = thread_create("test", basic_thread, counter);

    ASSERT(thread != thread_id(), "Newly created thread has same ID as us?");

    thread_info_t info;
    thread_info(thread, &info);

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
    global_counter_free(counter);
}

void *semaphore_thread(void *param)
{
    int profile = profile_start();
    semaphore_t *semaphore = param;

    semaphore_acquire(semaphore);

    unsigned int duration = profile_end(profile);

    for (volatile unsigned int i = 0; i < 1000000; i++) { ; }

    semaphore_release(semaphore);

    return (void *)duration;
}

void test_threads_semaphore(test_context_t *context)
{
    semaphore_t semaphore;
    semaphore_init(&semaphore, 2);

    uint32_t threads[5];
    unsigned int returns[5];
    unsigned int counts[4] = { 0, 0, 0, 0 };
    threads[0] = thread_create("test1", semaphore_thread, &semaphore);
    threads[1] = thread_create("test2", semaphore_thread, &semaphore);
    threads[2] = thread_create("test3", semaphore_thread, &semaphore);
    threads[3] = thread_create("test4", semaphore_thread, &semaphore);
    threads[4] = thread_create("test5", semaphore_thread, &semaphore);

    for(unsigned int i = 0; i < (sizeof(threads) / sizeof(threads[0])); i++)
    {
        thread_start(threads[i]);
    }

    for(unsigned int i = 0; i < (sizeof(threads) / sizeof(threads[0])); i++)
    {
        returns[i] = (uint32_t)thread_join(threads[i]);
    }

    unsigned int max_wait = 0;
    for(unsigned int i = 0; i < (sizeof(threads) / sizeof(threads[0])); i++)
    {
        max_wait = max_wait > returns[i] ? max_wait : returns[i];
    }

    for(unsigned int i = 0; i < (sizeof(threads) / sizeof(threads[0])); i++)
    {
        int bucket = (int)round(((double)returns[i] / (double)max_wait) * 2.0);
        if (bucket >= 0 && bucket <= 2)
        {
            counts[bucket]++;
        }
        else
        {
            counts[3]++;
        }
    }

    // Should have had two threads that waited no time.
    ASSERT(counts[0] == 2, "Unexpected number of threads %d that got semaphore immediately!", counts[0]);

    // Should have had two threads that waited for the first two threads to finish.
    ASSERT(counts[1] == 2, "Unexpected number of threads %d that got semaphore immediately!", counts[1]);

    // Should have had one thread that waited for the first two threads and the next two threads to finish.
    ASSERT(counts[2] == 1, "Unexpected number of threads %d that got semaphore immediately!", counts[2]);

    // Should have had no other buckets filled.
    ASSERT(counts[3] == 0, "Unexpected number of threads %d that got bizarre timing!", counts[3]);

    for(unsigned int i = 0; i < (sizeof(threads) / sizeof(threads[0])); i++)
    {
        thread_destroy(threads[i]);
    }
    semaphore_free(&semaphore);
}

void *mutex_try_thread(void *param)
{
    mutex_t *mutex = param;
    int got = 0;

    if (mutex_try_lock(mutex))
    {
        got = 1;

        for (volatile unsigned int i = 0; i < 1000000; i++) { ; }

        mutex_unlock(mutex);
    }

    return (void *)got;
}

void test_threads_mutex_trylock(test_context_t *context)
{
    mutex_t mutex;
    mutex_init(&mutex);

    uint32_t threads[2];
    int returns[2];

    threads[0] = thread_create("test1", mutex_try_thread, &mutex);
    threads[1] = thread_create("test2", mutex_try_thread, &mutex);

    thread_start(threads[0]);
    thread_start(threads[1]);

    returns[0] = (int)thread_join(threads[0]);
    returns[1] = (int)thread_join(threads[1]);

    ASSERT(
        (returns[0] == 0 && returns[1] == 1) || (returns[0] == 1 && returns[1] == 0),
        "Expected only one thread to acquire the mutex using a try lock!"
    );

    for(unsigned int i = 0; i < (sizeof(threads) / sizeof(threads[0])); i++)
    {
        thread_destroy(threads[i]);
    }
    mutex_free(&mutex);
}

void *mutex_lock_thread(void *param)
{
    int profile = profile_start();
    mutex_t *mutex = param;
    unsigned int duration = 0;

    mutex_lock(mutex);

    duration = profile_end(profile);

    for (volatile unsigned int i = 0; i < 1000000; i++) { ; }

    mutex_unlock(mutex);

    return (void *)duration;
}

void test_threads_mutex_lock(test_context_t *context)
{
    mutex_t mutex;
    mutex_init(&mutex);

    uint32_t threads[2];
    unsigned int returns[2];

    threads[0] = thread_create("test1", mutex_lock_thread, &mutex);
    threads[1] = thread_create("test2", mutex_lock_thread, &mutex);

    thread_start(threads[0]);
    thread_start(threads[1]);

    returns[0] = (int)thread_join(threads[0]);
    returns[1] = (int)thread_join(threads[1]);

    ASSERT(
        (returns[0] < 100 && returns[1] > 10000) || (returns[0] > 10000 && returns[1] < 100),
        "Expected one thread to have a long acquire time!"
    );


    for(unsigned int i = 0; i < (sizeof(threads) / sizeof(threads[0])); i++)
    {
        thread_destroy(threads[i]);
    }
    mutex_free(&mutex);
}

void *wait_thread(void *param)
{
    int profile = profile_start();
    timer_wait(250000);
    return ((void *)(uint32_t)profile_end(profile));
}

void *sleep_thread(void *param)
{
    int profile = profile_start();
    thread_sleep(250000);
    return ((void *)(uint32_t)profile_end(profile));
}

void test_threads_sleep(test_context_t *context)
{
    // First test wait.
    uint32_t thread = thread_create("test", wait_thread, 0);

    thread_priority(thread, MAX_PRIORITY - 1);
    thread_start(thread);
    uint32_t time_spent = (uint32_t)thread_join(thread);
    thread_destroy(thread);

    ASSERT(time_spent > 250000, "Did not wait enough time (%lu) in thread!", time_spent);
    ASSERT(time_spent < 251000, "Spent too much time (%lu) bookkeeping!", time_spent);

    // Now test sleep.
    thread = thread_create("test", sleep_thread, 0);

    thread_priority(thread, MAX_PRIORITY - 1);
    thread_start(thread);
    time_spent = (uint32_t)thread_join(thread);
    thread_destroy(thread);

    ASSERT(time_spent > 250000, "Did not wait enough time (%lu) in thread!", time_spent);
    ASSERT(time_spent < 251000, "Spent too much time (%lu) bookkeeping!", time_spent);
}
