#include <stdlib.h>
#include <math.h>

void test_malloc(test_context_t *context)
{
    uint32_t firstmalloc = (uint32_t)malloc(1024);
    uint32_t secondmalloc = (uint32_t)malloc(1024);

    // Simple test that ensures malloc is giving us valid RAM back
    ASSERT((firstmalloc & 0xFF000000) == 0x0C000000, "Invalid RAM location %08lx", firstmalloc);
    ASSERT((secondmalloc & 0xFF000000) == 0x0C000000, "Invalid RAM location %08lx", secondmalloc);

    int delta = abs((int)firstmalloc - (int)secondmalloc);

    ASSERT(delta >= 1024, "Allocations are too close together!");

    free((void *)firstmalloc);
    free((void *)secondmalloc);
}
