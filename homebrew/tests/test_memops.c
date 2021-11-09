#include <stdlib.h>
#include "naomi/video.h"
#include "naomi/system.h"

#define test_hw_memset_duration 200
void test_hw_memset(test_context_t *context)
{
    uint32_t *scratch = video_scratch_area();
    ASSERT((((uint32_t)scratch) & 0x1F) == 0, "Scratch region is not 32-byte aligned");

    // Zero a wider area so we can catch overspray.
    ASSERT(hw_memset(scratch, 0, 512), "Failed to get hardware for memset!");

    // Set 0xDEADBEEF to the center 256 bytes.
    ASSERT(hw_memset(scratch + (128 / 4), 0xDEADBEEF, 256), "Failed to get hardware for memset!");

    // Now check the 3 regions for overspray and correct contents.
    for (int i = 0; i < (128 / 4); i++)
    {
        ASSERT(scratch[i] == 0, "Unexpected value %08lx in VRAM location %d", scratch[i], i * 4);
    }
    for (int i = (128 / 4); i < ((128 + 256) / 4); i++)
    {
        ASSERT(scratch[i] == 0xDEADBEEF, "Unexpected value %08lx in VRAM location %d", scratch[i], i * 4);
    }
    for (int i = ((128 + 256) / 4); i < (512 / 4); i++)
    {
        ASSERT(scratch[i] == 0, "Unexpected value %08lx in VRAM location %d", scratch[i], i * 4);
    }
}

#define test_hw_memcpy_duration 300
void test_hw_memcpy(test_context_t *context)
{
    uint32_t *scratch = video_scratch_area();
    uint32_t *dest = scratch + (256 / 4);
    ASSERT((((uint32_t)scratch) & 0x1F) == 0, "Scratch region is not 32-byte aligned");

    // Set up an area with a pattern into RAM.
    uint32_t memval = 0xCAFEBABE;
    for (int i = 0; i < (256 / 4); i++)
    {
        scratch[i] = (memval + i) & 0xFFFFFFFF;
        memval = ((memval << 1) & 0xFFFFFFFE) | ((memval & 0x80000000) ? 1 : 0);
    }

    // Set up zeros around our copy area to catch overspray.
    ASSERT(hw_memset(dest, 0, 512), "Failed to get hardware for memset!");
    ASSERT(hw_memcpy(dest + (128 / 4), scratch, 256), "Failed to get hardware for memcpy!");

    for (int i = 0; i < (128 / 4); i++)
    {
        ASSERT(dest[i] == 0, "Unexpected value %08lx in VRAM location %d", dest[i], i * 4);
    }
    for (int i = ((128 + 256) / 4); i < (512 / 4); i++)
    {
        ASSERT(dest[i] == 0, "Unexpected value %08lx in VRAM location %d", dest[i], i * 4);
    }

    // Actually check the copied data.
    uint32_t expval = 0xCAFEBABE;
    for (int i = (128 / 4); i < ((128 + 256) / 4); i++)
    {
        uint32_t exp = ((expval + (i - (128 / 4))) & 0xFFFFFFFF);
        expval = ((expval << 1) & 0xFFFFFFFE) | ((expval & 0x80000000) ? 1 : 0);
        ASSERT(dest[i] == exp, "Unexpected value in VRAM location %d, %08lx != %08lx", i * 4, dest[i], exp);
    }
}
