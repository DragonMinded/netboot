#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "naomi/system.h"
#include "naomi/video.h"
#include "naomi/eeprom.h"
#include "naomi/console.h"
#include "naomi/timer.h"

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
        LOG("ASSERTION FAILED (%s:%d):\n  %s,\n  ", context->name, __LINE__, #condition); \
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
            LOG("ASSERTION FAILED (%s:%d):\n  %s[%d] != %s[%d],\n  ", context->name, __LINE__, #expected, __pos, #actual, __pos); \
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

void main()
{
    // Grab the system configuration
    eeprom_t settings;
    eeprom_read(&settings);

    // Set up a crude console.
    video_init_simple();
    video_set_background_color(rgb(0, 0, 0));
    console_init(16);

    printf("====================\n");
    printf("Starting tests\n%d tests to run\n", (sizeof(tests) / sizeof(tests[0])));
    printf("====================\n\n");
    video_display_on_vblank();

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
        video_display_on_vblank();

        test_context_t context;
        context.name = tests[testno].file;
        context.result = TEST_PASSED;
        context.log = logbuffer;
        context.logleft = sizeof(logbuffer);
        context.reason = reasonbuffer;
        context.reasonleft = sizeof(reasonbuffer);

        logbuffer[0] = 0;
        reasonbuffer[0] = 0;

        int profile = profile_start();
        tests[testno].main(&context);
        uint32_t nsec = profile_end(profile);
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
            printf("PASSED, %ldns\n", nsec);
            passed ++;
        }
        else if (context.result == TEST_SKIPPED)
        {
            if (strlen(reasonbuffer))
            {
                printf("SKIPPED, %ldns (%s)\n", nsec, reasonbuffer);
            }
            else
            {
                printf("SKIPPED, %ldns\n", nsec);
            }
            skipped ++;
        }
        else if (context.result == TEST_TOO_LONG)
        {
            if (strlen(reasonbuffer))
            {
                printf("FAILED, %ldns (%s)\n", nsec, reasonbuffer);
            }
            else
            {
                printf("FAILED, %ldns\n", nsec);
            }
            failed ++;
        }
        else if (context.result == TEST_FAILED)
        {
            if (strlen(reasonbuffer))
            {
                printf("FAILED, %ldns (%s)\n", nsec, reasonbuffer);
            }
            else
            {
                printf("FAILED, %ldns\n", nsec);
            }

            if (strlen(logbuffer))
            {
                printf("%s\n", logbuffer);
            }

            failed ++;
        }

        video_display_on_vblank();
    }

    printf("\n====================\n");
    printf("Finished\n%d pass, %d fail, %d skip\n%ldns total duration\n", passed, failed, skipped, total_duration);
    printf("====================\n");

    while ( 1 )
    {
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
