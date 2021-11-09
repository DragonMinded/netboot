// vim: set fileencoding=utf-8
#include <stdlib.h>
#include "naomi/system.h"

#define test_utf8_strlen_duration 10
void test_utf8_strlen(test_context_t *context)
{
    unsigned int result = utf8_strlen("");
    ASSERT(result == 0, "Invalid length %d for empty string", result);

    result = utf8_strlen("Hello!");
    ASSERT(result == 6, "Invalid length %d for normal string", result);

    result = utf8_strlen("こんにちは!");
    ASSERT(result == 6, "Invalid length %d for utf-8 string", result);
}

#define test_utf8_convert_duration 20
void test_utf8_convert(test_context_t *context)
{
    // Simple test that ensures malloc is giving us valid RAM back
    uint32_t *result = utf8_convert("");
    ASSERT(result[0] == 0, "Invalid byte %08lx for empty string", result[0]);
    free(result);

    result = utf8_convert("Hello!");
    uint32_t expectedascii[] = { 72, 101, 108, 108, 111, 33, 0 };
    ASSERT_ARRAYS_EQUAL(expectedascii, result, "Invalid ascii return");
    free(result);

    result = utf8_convert("こんにちは!");
    uint32_t expectedunicode[] = { 0x3053, 0x3093, 0x306b, 0x3061, 0x306f, 33, 0 };
    ASSERT_ARRAYS_EQUAL(expectedunicode, result, "Invalid unicode return");
    free(result);
}
