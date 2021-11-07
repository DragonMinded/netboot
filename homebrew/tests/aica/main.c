#include <stdint.h>

void main()
{
    volatile uint32_t *status = (volatile uint32_t *)0x10000;
    uint32_t status_higher = 0xCAFE;
    uint32_t status_lower = 0xBABE;
    status[0] = ((status_higher << 16) & 0xFFFF0000) | ((status_lower) & 0xFFFF);

    while( 1 )
    {
        ;
    }
}
