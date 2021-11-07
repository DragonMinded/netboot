#include <memory.h>
#include "naomi/audio.h"
#include "naomi/system.h"

#define AICA_BASE 0xA0700000

#define AICA_VERSION (0x2800 >> 2)
#define AICA_RESET (0x2C00 >> 2)

void load_aica_binary(void *binary, unsigned int length)
{
    // Note that for this to work without bugs or weirdness on the ARM side, the ARM
    // binary pointer should be 4-byte aligned, and the length should be a multiple
    // of 4. Any other configuration asks for undefined behavior.
    volatile unsigned int *aicabase = (volatile unsigned int *)AICA_BASE;

    // Set up 16-bit wave memory size.
    aicabase[AICA_VERSION] = 0x200;

    // Pull the AICA MCU in reset.
    aicabase[AICA_RESET] |= 0x1;

    // Copy the binary to the AICA MCU.
    unsigned int hw_copy_amount = length & 0xFFFFFFE0;
    if (hw_memcpy((void *)(SOUNDRAM_BASE | UNCACHED_MIRROR), binary, hw_copy_amount))
    {
        for(unsigned int memloc = hw_copy_amount; memloc < length; memloc+= 4)
        {
            *((uint32_t *)((SOUNDRAM_BASE | UNCACHED_MIRROR) + memloc)) = *((uint32_t *)(((uint32_t)binary) + memloc));
        }
    }
    else
    {
        // Failed to use HW memcpy, fall back to slow method.
        memcpy((void *)(SOUNDRAM_BASE | UNCACHED_MIRROR), binary, length);
    }

    // Pull the AICA MCU back out of reset.
    aicabase[AICA_RESET] &= ~0x1;
}
