#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "naomi/video.h"
#include "naomi/maple.h"

// Debug console
char *console_base = 0;
unsigned int console_loc = 0;
#define console_printf(...) do { sprintf(console_base + console_loc, __VA_ARGS__); console_loc += strlen(console_base + console_loc); } while(0)

void display()
{
    // Render a simple test console.
    video_fill_screen(rgb(48, 48, 48));
    video_draw_text(0, 0, rgb(255, 255, 255), console_base);
    video_wait_for_vblank();
    video_display();
}

void maple_print_regs()
{
    volatile unsigned int *maplebase = (volatile unsigned int *)MAPLE_BASE;

    console_printf("MAPLE_DMA_BUFFER_ADDR: %08X\n", maplebase[MAPLE_DMA_BUFFER_ADDR]);
    console_printf("MAPLE_DMA_TRIGGER_SELECT: %08X\n", maplebase[MAPLE_DMA_TRIGGER_SELECT]);
    console_printf("MAPLE_DEVICE_ENABLE: %08X\n", maplebase[MAPLE_DEVICE_ENABLE]);
    console_printf("MAPLE_DMA_START: %08X\n", maplebase[MAPLE_DMA_START]);
}

void maple_print_response(uint32_t *response)
{
    if(!maple_response_valid(response))
    {
        console_printf("Maple response is invalid.\n");
    }
    else
    {
        // Work around macro expansion bug by splitting these two up.
        console_printf("Response Code: %02X, ", maple_response_code(response));
        console_printf("Data length: %d\n", maple_response_payload_length_words(response));

        if(maple_response_payload_length_words(response) > 0)
        {
            console_printf("Data:");
            for (int i = 0; i < maple_response_payload_length_words(response); i++)
            {
                console_printf(" %08X", response[i+1]);

                // We can only reasonably fit 8 words on the screen.
                if(i % 8 == 7)
                {
                    console_printf("\n     ");
                }
            }
            console_printf("\n");
        }
    }
}

void maple_print_jvs_status(jvs_status_t status)
{
    console_printf("DIPSW: %X, ", status.dip_switches);
    console_printf("PSW1: %s, ", status.psw1 ? "pressed" : "released");
    console_printf("PSW2: %s, ", status.psw2 ? "pressed" : "released");
    console_printf("JVS IO: %s, ", status.jvs_present_bitmask & JVS_SENSE_DISCONNECTED ? "disconnected" : "connected");
    console_printf("%s\n", status.jvs_present_bitmask & JVS_SENSE_ADDRESSED ? "addressed" : "unaddressed");
    if(status.packet_length)
    {
        console_printf("Response packet length: %d\n", status.packet_length);
        console_printf("Response data:");
        for(int i = 0; i < status.packet_length; i++)
        {
            console_printf(" %02X", status.packet[i]);

            // We can only reasonably fit 8 words on the screen.
            if(i % 21 == 20)
            {
                console_printf("\n              ");
            }
        }
        console_printf("\n");
    }
    else
    {
        console_printf("No response packet received.\n");
    }
}

void main()
{
    // Set up a crude console
    video_init_simple();
    maple_init();
    console_base = malloc(((640 * 480) / (8 * 8)) + 1);
    memset(console_base, 0, ((640 * 480) / (8 * 8)) + 1);

    // Now, report on the memory test.
    if(maple_request_self_test())
    {
        console_printf("MIE reports healthy!\n");
    }
    else
    {
        console_printf("MIE reports bad RAM!\n");
    }
    display();

    // Request version, make sure we're running our updated code.
    char version[128];
    maple_request_version(version);
    console_printf("MIE version string: %s\n", version);
    display();

    // Now, display the JVS IO version ID.
    maple_request_jvs_id(0x01, version);
    console_printf("JVS IO ID: %s\n\n", version);
    display();

    // Now, read the controls forever.
    unsigned int reset_loc = console_loc;
    int liveness = 0;
    while ( 1 )
    {
        console_loc = reset_loc;
        console_printf("Liveness indicator: %d\n", liveness++);
        jvs_buttons_t buttons = maple_request_jvs_buttons(0x01, 2);

        console_printf("\n\nSystem buttons: ");
        if(buttons.dip1)
        {
            console_printf("dip1 ");
        }
        if(buttons.dip2)
        {
            console_printf("dip2 ");
        }
        if(buttons.dip3)
        {
            console_printf("dip3 ");
        }
        if(buttons.dip4)
        {
            console_printf("dip4 ");
        }
        if(buttons.psw1)
        {
            console_printf("psw1 ");
        }
        if(buttons.psw2)
        {
            console_printf("psw2 ");
        }
        if(buttons.test)
        {
            console_printf("test ");
        }
        console_printf("\n1P buttons: ");
        if(buttons.player1.service)
        {
            console_printf("svc ");
        }
        if(buttons.player1.start)
        {
            console_printf("start ");
        }
        if(buttons.player1.up)
        {
            console_printf("up ");
        }
        if(buttons.player1.down)
        {
            console_printf("down ");
        }
        if(buttons.player1.left)
        {
            console_printf("left ");
        }
        if(buttons.player1.right)
        {
            console_printf("right ");
        }
        if(buttons.player1.button1)
        {
            console_printf("b1 ");
        }
        if(buttons.player1.button2)
        {
            console_printf("b2 ");
        }
        if(buttons.player1.button3)
        {
            console_printf("b3 ");
        }
        if(buttons.player1.button4)
        {
            console_printf("b4 ");
        }
        if(buttons.player1.button5)
        {
            console_printf("b5 ");
        }
        if(buttons.player1.button6)
        {
            console_printf("b6 ");
        }
        console_printf("\n1P Analog: %02X %02X %02X %02X", buttons.player1.analog1, buttons.player1.analog2, buttons.player1.analog3, buttons.player1.analog4);
        console_printf("\n2P Buttons: ");
        if(buttons.player2.service)
        {
            console_printf("svc ");
        }
        if(buttons.player2.start)
        {
            console_printf("start ");
        }
        if(buttons.player2.up)
        {
            console_printf("up ");
        }
        if(buttons.player2.down)
        {
            console_printf("down ");
        }
        if(buttons.player2.left)
        {
            console_printf("left ");
        }
        if(buttons.player2.right)
        {
            console_printf("right ");
        }
        if(buttons.player2.button1)
        {
            console_printf("b1 ");
        }
        if(buttons.player2.button2)
        {
            console_printf("b2 ");
        }
        if(buttons.player2.button3)
        {
            console_printf("b3 ");
        }
        if(buttons.player2.button4)
        {
            console_printf("b4 ");
        }
        if(buttons.player2.button5)
        {
            console_printf("b5 ");
        }
        if(buttons.player2.button6)
        {
            console_printf("b6 ");
        }
        console_printf("\n2P Analog: %02X %02X %02X %02X\n", buttons.player2.analog1, buttons.player2.analog2, buttons.player2.analog3, buttons.player2.analog4);
        display();
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
