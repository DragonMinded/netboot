#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "naomi/video.h"
#include "naomi/maple.h"
#include "naomi/eeprom.h"
#include "naomi/console.h"

void main()
{
    // Grab the system configuration
    eeprom_t settings;
    eeprom_read(&settings);

    // Set up a crude console
    video_init_simple();
    video_set_background_color(rgb(48, 48, 48));
    console_init(16);

    // Now, report on the memory test.
    if(maple_request_self_test())
    {
        printf("MIE reports healthy!\n");
    }
    else
    {
        printf("MIE reports bad RAM!\n");
    }
    video_display_on_vblank();

    // Request version, make sure we're running our updated code.
    char version[128];
    maple_request_version(version);
    printf("MIE version string: %s\n", version);
    video_display_on_vblank();

    // Now, display the JVS IO version ID.
    maple_request_jvs_id(0x01, version);
    printf("JVS IO ID: %s\n\n", version);
    video_display_on_vblank();

    // Now, read the controls forever.
    char * reset_loc = console_save();
    int liveness = 0;

    while ( 1 )
    {
        // Put the console back to where it was before we entered the loop.
        console_restore(reset_loc);

        printf("Liveness indicator: %d\n", liveness++);
        jvs_buttons_t buttons;
        maple_request_jvs_buttons(0x01, &buttons);

        printf("\n\nSystem buttons: ");
        if(buttons.dip1)
        {
            printf("dip1 ");
        }
        if(buttons.dip2)
        {
            printf("dip2 ");
        }
        if(buttons.dip3)
        {
            printf("dip3 ");
        }
        if(buttons.dip4)
        {
            printf("dip4 ");
        }
        if(buttons.psw1)
        {
            printf("psw1 ");
        }
        if(buttons.psw2)
        {
            printf("psw2 ");
        }
        if(buttons.test)
        {
            printf("test ");
        }
        printf("\n1P buttons: ");
        if(buttons.player1.service)
        {
            printf("svc ");
        }
        if(buttons.player1.start)
        {
            printf("start ");
        }
        if(buttons.player1.up)
        {
            printf("up ");
        }
        if(buttons.player1.down)
        {
            printf("down ");
        }
        if(buttons.player1.left)
        {
            printf("left ");
        }
        if(buttons.player1.right)
        {
            printf("right ");
        }
        if(buttons.player1.button1)
        {
            printf("b1 ");
        }
        if(buttons.player1.button2)
        {
            printf("b2 ");
        }
        if(buttons.player1.button3)
        {
            printf("b3 ");
        }
        if(buttons.player1.button4)
        {
            printf("b4 ");
        }
        if(buttons.player1.button5)
        {
            printf("b5 ");
        }
        if(buttons.player1.button6)
        {
            printf("b6 ");
        }
        printf("\n1P Analog: %02X %02X %02X %02X", buttons.player1.analog1, buttons.player1.analog2, buttons.player1.analog3, buttons.player1.analog4);
        if (settings.system.players >= 2)
        {
            printf("\n2P Buttons: ");
            if(buttons.player2.service)
            {
                printf("svc ");
            }
            if(buttons.player2.start)
            {
                printf("start ");
            }
            if(buttons.player2.up)
            {
                printf("up ");
            }
            if(buttons.player2.down)
            {
                printf("down ");
            }
            if(buttons.player2.left)
            {
                printf("left ");
            }
            if(buttons.player2.right)
            {
                printf("right ");
            }
            if(buttons.player2.button1)
            {
                printf("b1 ");
            }
            if(buttons.player2.button2)
            {
                printf("b2 ");
            }
            if(buttons.player2.button3)
            {
                printf("b3 ");
            }
            if(buttons.player2.button4)
            {
                printf("b4 ");
            }
            if(buttons.player2.button5)
            {
                printf("b5 ");
            }
            if(buttons.player2.button6)
            {
                printf("b6 ");
            }
            printf("\n2P Analog: %02X %02X %02X %02X\n", buttons.player2.analog1, buttons.player2.analog2, buttons.player2.analog3, buttons.player2.analog4);
        }

        video_display_on_vblank();
    }
}

void test()
{
    video_init_simple();

    while ( 1 )
    {
        video_fill_screen(rgb(48, 48, 48));
        video_draw_debug_text(320 - 56, 236, rgb(255, 255, 255), "test mode stub");
        video_display_on_vblank();
    }
}
