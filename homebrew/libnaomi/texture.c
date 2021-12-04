#include <stdint.h>
#include "naomi/system.h"
#include "naomi/ta.h"

static int twiddletab[1024];
#define TWIDDLE(u, v) (twiddletab[(v)] | (twiddletab[(u)] << 1))

void _ta_init_twiddletab()
{
    for(int addr = 0; addr < 1024; addr++)
    {
        twiddletab[addr] = (
            (addr & 1) |
            ((addr & 2) << 1) |
            ((addr & 4) << 2) |
            ((addr & 8) << 3) |
            ((addr & 16) << 4) |
            ((addr & 32) << 5) |
            ((addr & 64) << 6) |
            ((addr & 128) << 7) |
            ((addr & 256) << 8) |
            ((addr & 512) << 9)
        );
    }
}

int ta_texture_load(void *offset, int uvsize, int bitsize, void *data)
{
    if (uvsize != 8 && uvsize != 16 && uvsize != 32 && uvsize != 64 && uvsize != 128 && uvsize != 256 && uvsize != 512 && uvsize != 1024)
    {
        return -1;
    }
    if (offset == 0 || data == 0)
    {
        return -1;
    }

    switch (bitsize)
    {
        case 8:
        {
            uint16_t *tex = (uint16_t *)(((uint32_t)offset) | UNCACHED_MIRROR);
            uint8_t *src = (uint8_t *)data;

            for(int v = 0; v < uvsize; v+= 2)
            {
                for(int u = 0; u < uvsize; u++)
                {
                    tex[TWIDDLE(u, v) >> 1] = src[(u + (v * uvsize))] | (src[u + ((v + 1) * uvsize)] << 8);
                }
            }
            break;
        }
        default:
        {
            // Currently only support loading 8bit textures here.
            return -1;
        }
    }

    return 0;
}
