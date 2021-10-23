#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "naomi/system.h"
#include "naomi/video.h"
#include "naomi/maple.h"
#include "naomi/eeprom.h"
#include "naomi/console.h"

void main()
{
    // Grab the system configuration
    eeprom_t settings;
    eeprom_read(&settings);

    // Set up a crude console.
    video_init_simple();
    video_set_background_color(rgb(48, 48, 48));
    console_init(16);

    // Now, read the RTC forever.
    char *reset_loc = console_save();

    while ( 1 )
    {
        // First poll buttons for a test mode request, since we specifically state that we
        // want the user to edit the time there.
        maple_poll_buttons();
        jvs_buttons_t pressed = maple_buttons_pressed();
        if (pressed.test || pressed.psw1)
        {
            enter_test_mode();
        }

        // Put the console back to where it was before we entered the loop.
        console_restore(reset_loc);

        time_t t = time(NULL);
        struct tm tm = *localtime(&t);

        printf("Current time: %d-%02d-%02d %02d:%02d:%02d\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        printf("Edit this time in the test menu under clock settings!");

        video_wait_for_vblank();
        video_display();
    }
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
