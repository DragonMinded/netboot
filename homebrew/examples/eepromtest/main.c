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
    maple_init();

    video_fill_screen(rgb(48, 48, 48));
    video_draw_text(0, 0, rgb(255, 255, 255), "Reading EEPROM...");

    uint8_t eeprom[128];
    memset(eeprom, 0, 128);

    if(maple_request_eeprom_read(eeprom) == 0)
    {
        video_draw_text(0, 8, rgb(0, 255, 0), "Success!");

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

        video_draw_text(0, 16, rgb(255, 255, 64), eeprom_buf);
    }
    else
    {
        video_draw_text(0, 8, rgb(255, 0, 0), "Failed!");
    }

    video_wait_for_vblank();
    video_display();

    while ( 1 ) { ; }
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
