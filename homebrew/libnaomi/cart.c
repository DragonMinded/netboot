#include <stdint.h>
#include "naomi/interrupt.h"
#include "naomi/cart.h"
#include "irqinternal.h"

#define CART_OFFSETH_REG *((uint16_t *)0xA05F7000)
#define CART_OFFSETL_REG *((uint16_t *)0xA05F7004)
#define CART_DATA_REG *((uint16_t *)0xA05F7008)

void cart_read(void *dst, uint32_t src, unsigned int len)
{
    if (((uint32_t)dst & 0x1) != 0)
    {
        _irq_display_invariant("invalid cart_read location", "dst %08lu is not aligned to a 2-byte boundary", (uint32_t)dst);
    }
    if ((src & 0x1) != 0)
    {
        _irq_display_invariant("invalid cart_read location", "src %08lu is not aligned to a 2-byte boundary", src);
    }
    if ((len & 0x1) != 0)
    {
        _irq_display_invariant("invalid cart_read amount", "len %u is not multiple of 2 bytes", len);
    }

    uint32_t old_irq = irq_disable();

    // Low address of cart read.
    CART_OFFSETL_REG = src & 0xFFFF;

    // High address of cart read, set bit 15 to signify auto advance address on read.
    CART_OFFSETH_REG = 0x8000 | ((src >> 16) & 0x0FFF);

    uint16_t *dstptr = (uint16_t *)dst;
    while (len > 0)
    {
        *dstptr = CART_DATA_REG;
        dstptr++;
        len-=2;
    }

    irq_restore(old_irq);
}

void cart_write(uint32_t dst, void *src, unsigned int len)
{
    if ((dst & 0x1) != 0)
    {
        _irq_display_invariant("invalid cart_write location", "dst %08lu is not aligned to a 2-byte boundary", dst);
    }
    if (((uint32_t )src & 0x1) != 0)
    {
        _irq_display_invariant("invalid cart_write location", "src %08lu is not aligned to a 2-byte boundary", (uint32_t)src);
    }
    if ((len & 0x1) != 0)
    {
        _irq_display_invariant("invalid cart_write amount", "len %u is not multiple of 2 bytes", len);
    }

    uint32_t old_irq = irq_disable();

    // Low address of cart write.
    CART_OFFSETL_REG = dst & 0xFFFF;

    // High address of cart write, set bit 15 to signify auto advance address on write.
    CART_OFFSETH_REG = 0x8000 | ((dst >> 16) & 0x0FFF);

    uint16_t *srcptr = (uint16_t *)src;
    while (len > 0)
    {
        CART_DATA_REG = *srcptr;;
        srcptr++;
        len-=2;
    }

    irq_restore(old_irq);
}
