#include <stdint.h>
#include <string.h>
#include "naomi/interrupt.h"
#include "naomi/cart.h"
#include "irqinternal.h"

#define CART_OFFSETH_REG *((uint16_t *)0xA05F7000)
#define CART_OFFSETL_REG *((uint16_t *)0xA05F7004)
#define CART_DATA_REG *((uint16_t *)0xA05F7008)

static int header_read = 0;
static uint32_t header[HEADER_SIZE >> 2];

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

void cart_read_rom_header(void *dst)
{
    // Reads the first HEADER_SIZE bytes of the ROM header into destination pointer dst.
    uint32_t old_irq = irq_disable();

    if (header_read == 0)
    {
        cart_read(header, 0, HEADER_SIZE);
        header_read = 1;
    }

    memcpy(dst, header, HEADER_SIZE);
    irq_restore(old_irq);
}

void cart_read_executable_info(executable_t *exe)
{
    // First, make sure the header is actually read.
    {
        uint32_t old_irq = irq_disable();

        if (header_read == 0)
        {
            cart_read(header, 0, HEADER_SIZE);
            header_read = 1;
        }

        irq_restore(old_irq);
    }

    // Now, copy out sections.
    if (exe)
    {
        uint8_t *headerbytes = (uint8_t *)header;
        memcpy(&exe->main_entrypoint, &headerbytes[0x420], 4);
        memcpy(&exe->test_entrypoint, &headerbytes[0x424], 4);

        exe->main_section_count = 0;
        for (int i = 0; i < 8; i++)
        {
            uint32_t offset;
            memcpy(&offset, &headerbytes[0x360 + (12 * i)], 4);

            if (offset != 0xFFFFFFFF)
            {
                exe->main[i].offset = offset;
                memcpy(&exe->main[i].load_address, &headerbytes[0x360 + (12 * i) + 4], 4);
                memcpy(&exe->main[i].length, &headerbytes[0x360 + (12 * i) + 8], 4);
                exe->main_section_count = i + 1;
            }
            else
            {
                break;
            }
        }

        exe->test_section_count = 0;
        for (int i = 0; i < 8; i++)
        {
            uint32_t offset;
            memcpy(&offset, &headerbytes[0x3C0 + (12 * i)], 4);

            if (offset != 0xFFFFFFFF)
            {
                exe->test[i].offset = offset;
                memcpy(&exe->test[i].load_address, &headerbytes[0x3C0 + (12 * i) + 4], 4);
                memcpy(&exe->test[i].length, &headerbytes[0x3C0 + (12 * i) + 8], 4);
                exe->test_section_count = i + 1;
            }
            else
            {
                break;
            }
        }
    }
}
