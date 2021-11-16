#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "naomi/system.h"
#include "naomi/video.h"
#include "naomi/eeprom.h"
#include "naomi/console.h"
#include "naomi/timer.h"
#include "naomi/message/message.h"

typedef struct {
    const char * name;
    int result;
    char *log;
    int logleft;
    char *reason;
    int reasonleft;
} test_context_t;

typedef void (*test_func_t)(test_context_t *context);

#define TEST_PASSED 0
#define TEST_FAILED 1
#define TEST_SKIPPED 2
#define TEST_TOO_LONG 3

#define LOG(msg, ...) \
do { \
    if (context->logleft) { \
        int __len = snprintf(context->log, context->logleft, msg, ##__VA_ARGS__); \
        context->log += __len; \
        context->logleft -= __len; \
    } \
} while(0)

#define ASSERT(condition, msg, ...) \
do { \
    if (!(condition)) { \
        context->result = TEST_FAILED; \
        int __len = snprintf(context->reason, context->reasonleft, "assertion failure"); \
        context->reason += __len; \
        context->reasonleft -= __len; \
        LOG("%c[31mASSERTION FAILED (%s:%d)%c[0m:\n  %s,\n  ", 0x1B, context->name, __LINE__, 0x1B, #condition); \
        LOG(msg, ##__VA_ARGS__); \
        return; \
    } \
} while(0)

#define ASSERT_ARRAYS_EQUAL(expected, actual, msg, ...) \
do { \
    for (unsigned int __pos = 0; __pos < (sizeof(expected) / sizeof((expected)[0])); __pos++) { \
        if (expected[__pos] != actual[__pos]) { \
            context->result = TEST_FAILED; \
            int __len = snprintf(context->reason, context->reasonleft, "assertion failure"); \
            context->reason += __len; \
            context->reasonleft -= __len; \
            LOG("%c[31mASSERTION FAILED (%s:%d)%c[0m:\n  %s[%d] != %s[%d],\n  ", 0x1B, context->name, __LINE__, 0x1B, #expected, __pos, #actual, __pos); \
            LOG(msg, ##__VA_ARGS__); \
            return; \
        } \
    } \
} while(0)


#define SKIP(msg, ...) \
do { \
    context->result = TEST_SKIPPED; \
    int __len = snprintf(context->reason, context->reasonleft, msg, ##__VA_ARGS__); \
    context->reason += __len; \
    context->reasonleft -= __len; \
    return; \
} while(0)

// =================================================
// TEST FILES SECTION
// =================================================

// Note that this section will be automatically filled in by finding
// test files that look like test_*.c.

// =================================================
// END TEST FILES SECTION
// =================================================

#define TESTCASE(file, fn, dur) { file, #fn, fn, dur }
static const struct test_suite
{
    const char * const file;
    const char * const name;
    test_func_t main;
    int duration;
} tests[] = {
// =================================================
// TEST CASES SECTION
// =================================================

// Note that this section will be automatically filled in by finding
// test files that look like test_*.c.

// =================================================
// END TEST CASES SECTION
// =================================================
};

void * video(void * param)
{
    // Set up a crude console.
    video_init_simple();
    video_set_background_color(rgb(0, 0, 0));
    console_init(16);

    // Signal to the main thread that we're up.
    global_counter_increment(param);

    // Now just display the console.
    while( 1 )
    {
        video_display_on_vblank();
    }
}

#define CYAN (char []){ 0x1B, '[', '3', '6', 'm', 0 }
#define RED (char []){ 0x1B, '[', '3', '1', 'm', 0 }
#define GREEN (char []){ 0x1B, '[', '3', '2', 'm', 0 }
#define YELLOW (char []){ 0x1B, '[', '2', ';', '3', '3', 'm', 0 }
#define RESET (char []){ 0x1B, '[', '0', 'm', 0 }

void run_suite()
{
    printf("====================\n");
    printf("Starting tests\n%s%d tests to run%s\n", CYAN, (sizeof(tests) / sizeof(tests[0])), RESET);
    printf("====================\n\n");

    // Run the tests!
    char logbuffer[2048];
    char reasonbuffer[128];
    unsigned int passed = 0;
    unsigned int failed = 0;
    unsigned int skipped = 0;
    uint32_t total_duration = 0;

    for (unsigned int testno = 0; testno < (sizeof(tests) / sizeof(tests[0])); testno++)
    {
        // Set the test up to be run.
        printf("%s...", tests[testno].name);

        test_context_t context;
        context.name = tests[testno].file;
        context.result = TEST_PASSED;
        context.log = logbuffer;
        context.logleft = sizeof(logbuffer);
        context.reason = reasonbuffer;
        context.reasonleft = sizeof(reasonbuffer);

        logbuffer[0] = 0;
        reasonbuffer[0] = 0;

        uint32_t nsec;
        if (tests[testno].duration > 0)
        {
            /* Timing-critical test */
            uint32_t irq = irq_disable();
            int profile = profile_start();
            tests[testno].main(&context);
            nsec = profile_end(profile);
            irq_restore(irq);
        }
        else
        {
            /* Regular test */
            int profile = profile_start();
            tests[testno].main(&context);
            nsec = profile_end(profile);
        }
        total_duration += nsec;

        if (context.result == TEST_PASSED && tests[testno].duration >= 0)
        {
            if (nsec > tests[testno].duration)
            {
                context.result = TEST_TOO_LONG;
                snprintf(context.reason, context.reasonleft, "duration larger than %d", tests[testno].duration);
            }
        }

        if (context.result == TEST_PASSED)
        {
            printf("%sPASSED%s, %s%ldns%s\n", GREEN, RESET, CYAN, nsec, RESET);
            passed ++;
        }
        else if (context.result == TEST_SKIPPED)
        {
            if (strlen(reasonbuffer))
            {
                printf("%sSKIPPED%s, %s%ldns%s (%s)\n", YELLOW, RESET, CYAN, nsec, RESET, reasonbuffer);
            }
            else
            {
                printf("%sSKIPPED%s, %s%ldns%s\n", YELLOW, RESET, CYAN, nsec, RESET);
            }
            skipped ++;
        }
        else if (context.result == TEST_TOO_LONG || context.result == TEST_FAILED)
        {
            if (strlen(reasonbuffer))
            {
                printf("%sFAILED%s, %s%ldns%s (%s)\n", RED, RESET, CYAN, nsec, RESET, reasonbuffer);
            }
            else
            {
                printf("%sFAILED%s, %s%ldns%s\n", RED, RESET, CYAN, nsec, RESET);
            }
            failed ++;

            if (context.result == TEST_FAILED && strlen(logbuffer))
            {
                printf("%s\n", logbuffer);
            }
        }
    }

    printf("\n====================\n");
    printf("Finished\n%s%d pass%s, %s%d fail%s, %s%d skip%s\n%s%ldns total duration%s\n", GREEN, passed, RESET, RED, failed, RESET, YELLOW, skipped, RESET, CYAN, total_duration, RESET);
    printf("====================\n");
}

void main()
{
    // Set up a video refresh screen so we can just printf in the main thread.
    void *counter = global_counter_init(0);
    uint32_t video_thread = thread_create("video", video, counter);
    thread_priority(video_thread, 1);
    thread_start(video_thread);

    // Wait until the video thread has started up before trying to print to the console.
    while (global_counter_value(counter) == 0) { thread_yield(); }
    global_counter_free(counter);

    // Allow the printf messages to go to a connected host as well.
    message_init();
    message_stdio_redirect_init();

    // Run the test suite!
    run_suite();

    // Park it forever.
    while ( 1 ) { ; }
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
