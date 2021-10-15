#include <stdio.h>
#include <stdint.h>
#include "naomi/video.h"


#define REG_A05F6904 ((volatile uint32_t *)0xA05F6904)
#define REG_A05F6914 ((volatile uint32_t *)0xA05F6914)
#define NAOMI_DIMM_COMMAND ((volatile uint16_t *)0xA05F703C)
#define NAOMI_DIMM_OFFSETL ((volatile uint16_t *)0xA05F7040)
#define NAOMI_DIMM_PARAMETERL ((volatile uint16_t *)0xA05F7044)
#define NAOMI_DIMM_PARAMETERH ((volatile uint16_t *)0xA05F7048)
#define NAOMI_DIMM_STATUS ((volatile uint16_t *)0xA05F704C)
#define REG_A05F7418 ((volatile int32_t *)0xA05F7418)

#define CONST_NO_DIMM 0xFFFF
#define CONST_DIMM_HAS_COMMAND 0x8000
#define CONST_DIMM_COMMAND_MASK 0x7E00
#define CONST_DIMM_TOP_MASK 0x1FF


int check_has_dimm_inserted(int param_1)
{
    if ((param_1 != 0) && (*REG_A05F7418 != 0)) {
        return 0;
    }
    if (*NAOMI_DIMM_COMMAND == CONST_NO_DIMM) {
        return -1;
    }
    return 1;
}

void marshall_dimm_command()
{
    static uint32_t base_address = 0;

    if (*REG_A05F7418 == 0) {
        // Do stuff here
        uint16_t dimm_command = *NAOMI_DIMM_COMMAND;

        if (dimm_command & CONST_DIMM_HAS_COMMAND) {
            // Get the command ID
            unsigned int dimm_command_id = (dimm_command & CONST_DIMM_COMMAND_MASK) >> 9;
            unsigned char offseth = 0;
            unsigned short offsetl = 0;
            unsigned short paraml = 0;
            unsigned short paramh = 0;

            switch (dimm_command_id) {
                case 0:
                {
                    /* NOOP command */
                    break;
                }
                case 1:
                {
                    /* Unknown lookup of some BIOS value. We don't implement this. */
                    break;
                }
                case 3:
                {
                    /* Update base address */
                    base_address = (((*NAOMI_DIMM_PARAMETERH) & 0xFFFF) << 16) | ((*NAOMI_DIMM_PARAMETERL) & 0xFFFF);
                    break;
                }
                case 4:
                {
                    /* Peek 8-bit value out of memory */
                    uint32_t address = (((dimm_command & CONST_DIMM_TOP_MASK) << 16) | (*NAOMI_DIMM_OFFSETL) & 0xFFFF) + base_address;
                    paramh = 0;
                    paraml = 0xAA;
                    break;
                }
                case 5:
                {
                    /* Peek 16-bit value out of memory */
                    uint32_t address = (((dimm_command & CONST_DIMM_TOP_MASK) << 16) | (*NAOMI_DIMM_OFFSETL) & 0xFFFF) + base_address;
                    paramh = 0;
                    paraml = 0xCAFE;
                    break;
                }
                case 6:
                {
                    /* Peek 32-bit value out of memory */
                    uint32_t address = (((dimm_command & CONST_DIMM_TOP_MASK) << 16) | (*NAOMI_DIMM_OFFSETL) & 0xFFFF) + base_address;
                    paramh = 0xDEAD;
                    paraml = 0xBEEF;
                    break;
                }
                case 8:
                {
                    /* Poke 8-bit value into memory */
                    uint32_t address = (((dimm_command & CONST_DIMM_TOP_MASK) << 16) | (*NAOMI_DIMM_OFFSETL) & 0xFFFF) + base_address;
                    uint8_t value = (*NAOMI_DIMM_PARAMETERL) & 0xFF;
                    break;
                }
                case 9:
                {
                    /* Poke 16-bit value into memory */
                    uint32_t address = (((dimm_command & CONST_DIMM_TOP_MASK) << 16) | (*NAOMI_DIMM_OFFSETL) & 0xFFFF) + base_address;
                    uint16_t value = (*NAOMI_DIMM_PARAMETERL) & 0xFFFF;
                    break;
                }
                case 10:
                {
                    /* Poke 32-bit value into memory */
                    uint32_t address = (((dimm_command & CONST_DIMM_TOP_MASK) << 16) | (*NAOMI_DIMM_OFFSETL) & 0xFFFF) + base_address;
                    uint32_t value = (((*NAOMI_DIMM_PARAMETERH) & 0xFFFF) << 16) | ((*NAOMI_DIMM_PARAMETERL) & 0xFFFF);
                    break;
                }
            }

            // Acknowledge the command, return the response.
            *NAOMI_DIMM_COMMAND = (dimm_command & CONST_DIMM_COMMAND_MASK) | (offseth & 0xFF);
            *NAOMI_DIMM_OFFSETL = offsetl;
            *NAOMI_DIMM_PARAMETERL = paraml;
            *NAOMI_DIMM_PARAMETERH = paramh;
            *NAOMI_DIMM_STATUS = *NAOMI_DIMM_STATUS | 0x100;

            do {
                /* Do some spinlop to wait for some other register. */
            } while ((*REG_A05F6904 & 8) != 0);

            /* Send interrupt to the DIMM itself saying we have data. */
            *NAOMI_DIMM_STATUS = *NAOMI_DIMM_STATUS & 0xFFFE;
        }
        else {
            /* Acknowledge the command */
            *NAOMI_DIMM_STATUS = *NAOMI_DIMM_STATUS | 0x100;
            do {
                /* Do some spinloop to wait for some other register. */
            } while ((*REG_A05F6904 & 8) != 0);
        }
    }
    else {
        // Some other acknowledge?
        *REG_A05F6914 = *REG_A05F6914 & 0xfffffff7;
    }
}

void main()
{
    video_init_simple();

    char buffer[64];
    unsigned int counter = 0;

    while ( 1 )
    {
        // Draw a few simple things on the screen.
        video_fill_screen(rgb(48, 48, 48));
        video_draw_text(100, 180, rgb(255, 255, 255), "Net Dimm communications test stub.");
        video_draw_text(100, 200, rgb(255, 0, 255), "Use the peek/poke commands to talk to this code!");

        // Display a liveness counter that goes up 60 times a second.
        sprintf(buffer, "Aliveness counter: %d", counter++);
        video_draw_text(100, 220, rgb(200, 200, 20), buffer);

        // Copy BIOS DIMM service routine basics.
        if (check_has_dimm_inserted(1) == 1) {
            marshall_dimm_command();
        }

        // Actually draw the buffer.
        video_wait_for_vblank();
        video_display();
    }
}

void test()
{
    video_init_simple();

    video_fill_screen(rgb(48, 48, 48));
    video_draw_text(320 - 56, 236, rgb(255, 255, 255), "test mode stub");
    video_wait_for_vblank();
    video_display();

    while ( 1 ) { ; }
}
