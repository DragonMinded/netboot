#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "naomi/video.h"
#include "naomi/eeprom.h"
#include "naomi/thread.h"
#include "naomi/interrupt.h"
#include "naomi/timer.h"
#include "naomi/rtc.h"

void *thread1(void *param)
{
    char *buf = param;
    uint32_t counter = 0;

    while ( 1 )
    {
        uint32_t id = thread_id();
        thread_info_t info = thread_info(id);
        ATOMIC(sprintf(buf, "Thread ID: %ld, Thread Name: %s, CPU: %.02f percent\nCounter: %ld", id, info.name, info.cpu_percentage * 100.0, counter));
        counter += 1;
    }

    return 0;
}

void *thread2(void *param)
{
    char *buf = param;
    uint32_t counter = 0;

    while ( 1 )
    {
        uint32_t id = thread_id();
        thread_info_t info = thread_info(id);
        ATOMIC(sprintf(buf, "Thread ID: %ld, Thread Name: %s, CPU: %.02f percent\nCounter: %ld", id, info.name, info.cpu_percentage * 100.0, counter));
        counter += 2;
    }

    return 0;
}

void *thread3(void *param)
{
    char *buf = param;
    uint32_t counter = 0;

    while ( 1 )
    {
        uint32_t id = thread_id();
        thread_info_t info = thread_info(id);
        ATOMIC(sprintf(buf, "Thread ID: %ld, Thread Name: %s, CPU: %.02f percent\nCounter: %ld", id, info.name, info.cpu_percentage * 100.0, counter));
        counter += 3;
    }

    return 0;
}

void *thread4(void *param)
{
    char *buf = param;
    uint32_t counter = 0;

    while ( 1 )
    {
        uint32_t id = thread_id();
        thread_info_t info = thread_info(id);
        ATOMIC(sprintf(buf, "Thread ID: %ld, Thread Name: %s, CPU: %.02f percent\nCounter: %ld", id, info.name, info.cpu_percentage * 100.0, counter));
        counter += 4;
    }

    return 0;
}

void *thread5(void *param)
{
    char *buf = param;
    uint32_t counter = 0;

    while ( 1 )
    {
        uint32_t id = thread_id();
        thread_info_t info = thread_info(id);
        ATOMIC(sprintf(buf, "Thread ID: %ld, Thread Name: %s, CPU: %.02f percent\nCounter: %ld\nRTC: %lu", id, info.name, info.cpu_percentage * 100.0, counter, rtc_get()));

        timer_wait(500000);
        counter ++;
    }

    return 0;
}

void *thread6(void *param)
{
    char *buf = param;
    uint32_t counter = 0;

    while ( 1 )
    {
        uint32_t id = thread_id();
        thread_info_t info = thread_info(id);
        ATOMIC(sprintf(buf, "Thread ID: %ld, Thread Name: %s, CPU: %.02f percent\nCounter: %ld\nRTC: %lu", id, info.name, info.cpu_percentage * 100.0, counter, rtc_get()));

        thread_sleep(500000);
        counter ++;
    }

    return 0;
}

void main()
{
    // Grab the system configuration
    eeprom_t settings;
    eeprom_read(&settings);

    // Set up a crude console.
    video_init_simple();
    video_set_background_color(rgb(48, 48, 48));

    // Create a simple buffer for threads to manipulate.
    char tbuf[7][256];

    // Create four threads, each with their own function.
    uint32_t threads[6];

    threads[0] = thread_create("thread1", thread1, tbuf[1]);
    threads[1] = thread_create("thread2", thread2, tbuf[2]);
    threads[2] = thread_create("thread3", thread3, tbuf[3]);
    threads[3] = thread_create("thread4", thread4, tbuf[4]);
    threads[4] = thread_create("thread5", thread5, tbuf[5]);
    threads[5] = thread_create("thread6", thread6, tbuf[6]);

    // Start them all.
    for (unsigned int i = 0; i < (sizeof(threads) / sizeof(threads[0])); i++)
    {
        thread_start(threads[i]);
    }

    uint32_t frame_counter = 0;

    while ( 1 )
    {
        // Display our own threading info.
        uint32_t id = thread_id();
        thread_info_t info = thread_info(id);
        sprintf(tbuf[0], "Thread ID: %ld, Thread Name: %s, CPU: %.02f percent\nFrame Counter: %ld", id, info.name, info.cpu_percentage * 100.0, frame_counter);

        // Go through and display all 5 buffers.
        for (unsigned int i = 0; i < (sizeof(tbuf) / sizeof(tbuf[0])); i++)
        {
            video_draw_debug_text(50, 50 + (45 * i), rgb(255, 255, 255), tbuf[i]);
        }

        video_display_on_vblank();
    }
}

void test()
{
    video_init_simple();

    while ( 1 )
    {
        video_fill_screen(rgb(48, 48, 48));
        video_draw_debug_text(320 - 56, 236, rgb(255, 255, 255), "test mode stub");
        video_display_on_vblank();
    }
}
