#include "clib.h"

void * memset(void *ptr, int value, size_t num)
{
    uint32_t location = (uint32_t)ptr;
    uint32_t value32 = value & 0xFF | ((value << 8) & 0xFF00) | ((value << 16) & 0xFF0000) | ((value << 24) & 0xFF000000);

    while (num > 0)
    {
        // Clear any prefixed misaligned bits.
        while (num > 0 && location & 0x3)
        {
            *((uint8_t *)location++) = value;
            num--;
        }

        // Clear aligned faster now.
        while (num >= 4)
        {
            *((uint32_t *)location) = value;
            num -= 4;
            location += 4;
        }

        // Clear any dangling misaligned bits.
        while (num > 0)
        {
            *((uint8_t *)location++) = value;
            num--;
        }
    }

    return ptr;
}
