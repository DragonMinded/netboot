#include <stdlib.h>
#include <malloc.h>
#include "naomi/ta.h"

void test_ta_malloc(test_context_t *context)
{
    // First, get ahold of the textram stats before we do anything.
    struct mallinfo before = ta_texture_mallinfo();
    ASSERT(before.arena > 0, "Expected at least 1 byte available in TEXRAM");
    ASSERT(before.fordblks == before.arena, "Expected entire TEXRAM available");
    ASSERT(before.uordblks == 0, "Expected no allocations in TEXRAM");

    uint32_t firstmalloc = (uint32_t)ta_texture_malloc(256, 8);

    struct mallinfo after = ta_texture_mallinfo();
    ASSERT(after.arena == before.arena, "Expected arena size not to change");
    ASSERT(after.fordblks == before.arena - (256 * 256), "Expected some bytes to be allocated");
    ASSERT(after.uordblks == 256 * 256, "Expected some bytes to be allocated");

    uint32_t secondmalloc = (uint32_t)ta_texture_malloc(256, 8);

    after = ta_texture_mallinfo();
    ASSERT(after.arena == before.arena, "Expected arena size not to change");
    ASSERT(after.fordblks == before.arena - (256 * 256 * 2), "Expected some bytes to be allocated");
    ASSERT(after.uordblks == 256 * 256 * 2, "Expected some bytes to be allocated");

    // Simple test that ensures malloc is giving us valid RAM back
    ASSERT((firstmalloc & 0xFF000000) == 0xA4000000, "Invalid RAM location %08lx", firstmalloc);
    ASSERT((secondmalloc & 0xFF000000) == 0xA4000000, "Invalid RAM location %08lx", secondmalloc);

    int delta = abs((int)firstmalloc - (int)secondmalloc);

    ASSERT(delta >= (256 * 256), "Allocations are too close together!");

    ta_texture_free((void *)firstmalloc);

    after = ta_texture_mallinfo();
    ASSERT(after.arena == before.arena, "Expected arena size not to change");
    ASSERT(after.fordblks == before.arena - (256 * 256), "Expected some bytes to be allocated");
    ASSERT(after.uordblks == 256 * 256, "Expected some bytes to be allocated");

    ta_texture_free((void *)secondmalloc);

    // Finally, make sure we're back to the old values.
    after = ta_texture_mallinfo();
    ASSERT(after.arena == before.arena, "Expected arena size not to change");
    ASSERT(after.fordblks == before.arena, "Expected entire TEXRAM available");
    ASSERT(after.uordblks == 0, "Expected no allocations in TEXRAM");
}
