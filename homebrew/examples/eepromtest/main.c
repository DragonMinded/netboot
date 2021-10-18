#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "naomi/video.h"
#include "naomi/maple.h"

void main()
{
    // Set up a crude console
    video_init_simple();

    video_fill_screen(rgb(48, 48, 48));
    video_draw_debug_text(0, 0, rgb(255, 255, 255), "Reading EEPROM...");

    uint8_t eeprom[128];
    memset(eeprom, 0, 128);

    // First, try to read, bail out of it fails.
    if(maple_request_eeprom_read(eeprom) == 0)
    {
        video_draw_debug_text(0, 8, rgb(0, 255, 0), "Success!");

        char eeprom_buf[512];
        memset(eeprom_buf, 0, 512);
        for(int i = 0; i < 128; i++)
        {
            sprintf(
                eeprom_buf + strlen(eeprom_buf),
                "%02X ",
                eeprom[i]
            );

            if(i % 16 == 15)
            {
                sprintf(eeprom_buf + strlen(eeprom_buf), "\n");
            }
        }

        video_draw_debug_text(0, 16, rgb(255, 255, 64), eeprom_buf);

        // Now, try to write an update and read it back again.
        video_draw_debug_text(0, 80, rgb(255, 255, 255), "Writing new EEPROM...");
        eeprom[120] = ~eeprom[120];
        eeprom[121] = ~eeprom[121];
        eeprom[122] = ~eeprom[122];
        eeprom[123] = ~eeprom[123];
        eeprom[124] = 0xDE;
        eeprom[125] = 0xAD;
        eeprom[126] = 0xBE;
        eeprom[127] = 0xEF;
        if(maple_request_eeprom_write(eeprom) == 0)
        {
            video_draw_debug_text(0, 88, rgb(0, 255, 0), "Success!");

            // Finally, re-read to verify the update worked
            video_draw_debug_text(0, 96, rgb(255, 255, 255), "Reading EEPROM again to verify...");

            // First, try to read, bail out of it fails.
            memset(eeprom, 0, 128);
            if(maple_request_eeprom_read(eeprom) == 0)
            {
                video_draw_debug_text(0, 104, rgb(0, 255, 0), "Success!");

                memset(eeprom_buf, 0, 512);
                for(int i = 0; i < 128; i++)
                {
                    sprintf(
                        eeprom_buf + strlen(eeprom_buf),
                        "%02X ",
                        eeprom[i]
                    );

                    if(i % 16 == 15)
                    {
                        sprintf(eeprom_buf + strlen(eeprom_buf), "\n");
                    }
                }

                video_draw_debug_text(0, 112, rgb(255, 255, 64), eeprom_buf);
            }
            else
            {
                video_draw_debug_text(0, 104, rgb(255, 0, 0), "Failed!");
            }
        }
        else
        {
            video_draw_debug_text(0, 88, rgb(255, 0, 0), "Failed!");
        }
    }
    else
    {
        video_draw_debug_text(0, 8, rgb(255, 0, 0), "Failed!");
    }

    video_wait_for_vblank();
    video_display();

    while ( 1 ) { ; }
}

void test()
{
    video_init_simple();

    while ( 1 )
    {
        video_fill_screen(rgb(48, 48, 48));
        video_draw_debug_text(320 - 56, 236, rgb(255, 255, 255), "test mode stub");
        video_wait_for_vblank();
        video_display();
    }
}
