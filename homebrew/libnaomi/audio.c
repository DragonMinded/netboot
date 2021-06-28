#include <memory.h>
#include "naomi/audio.h"
#include "naomi/system.h"

void load_aica_binary(void *binary, unsigned int length)
{
    volatile unsigned int *aicabase = (volatile unsigned int *)AICA_BASE;

    // Set up 16-bit wave memory size.
    aicabase[AICA_VERSION] = 0x200;

    // Pull the AICA MCU in reset.
    aicabase[AICA_RESET] |= 0x1;

    // Do the BSS-init for the ARM binary.
    for(int memloc = 0; memloc < SOUNDRAM_SIZE; memloc+= 4)
    {
        *((uint32_t *)((SOUNDRAM_BASE | UNCACHED_MIRROR) + memloc)) = 0;
    }

    // Copy the binary to the AICA MCU.
    for(int memloc = 0; memloc < length; memloc+= 4)
    {
        *((uint32_t *)((SOUNDRAM_BASE | UNCACHED_MIRROR) + memloc)) = *((uint32_t *)(((uint32_t)binary) + memloc));
    }

    // Pull the AICA MCU back out of reset.
    aicabase[AICA_RESET] &= ~0x1;
}
