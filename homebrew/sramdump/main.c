#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <naomi/video.h>
#include <naomi/system.h>
#include <naomi/message/message.h>

#define MESSAGE_READY 0x2000
#define MESSAGE_SRAM_READ_REQUEST 0x2001
#define MESSAGE_SRAM_WRITE_REQUEST 0x2002
#define MESSAGE_SRAM_READ 0x2003
#define MESSAGE_SRAM_WRITE 0x2004
#define MESSAGE_DONE 0x2005

#define OPERATION_NONE 0
#define OPERATION_READ 1
#define OPERATION_WRITE 2

void draw_centered_text(int y, color_t color, char * msg)
{
    int width = strlen(msg) * 8;
    video_draw_debug_text((video_width() - width) / 2, y, color, msg);
}

void main()
{
    // We just want a simple framebuffer display.
    video_init(VIDEO_COLOR_1555);
    video_set_background_color(rgb(48, 48, 48));

    // Initialize message library
    message_init();
    message_stdio_redirect_init();

    draw_centered_text((video_height() / 2) - 4, rgb(255, 255, 255), "Waiting for host command...");
    video_display_on_vblank();

    // Inform the host we are alive and ready.
    message_send(MESSAGE_READY, NULL, 0);

    // Wait for a command to operate on.
    int operation = OPERATION_NONE;
    while (operation == OPERATION_NONE)
    {
        uint16_t type = 0;
        uint8_t *data = 0;
        unsigned int length = 0;
        if (message_recv(&type, (void *)&data, &length) == 0)
        {
            if (type == MESSAGE_SRAM_READ_REQUEST && length == 0)
            {
                operation = OPERATION_READ;
            }
            else if (type == MESSAGE_SRAM_WRITE_REQUEST && length == 0)
            {
                operation = OPERATION_WRITE;
            }
            else
            {
                printf("Unexpected packet %04X with length %d!\n", type, length);
            }

            // Wipe any data that came in.
            if (data != 0)
            {
                free(data);
            }
        }
    }

    // Run the operation.
    switch (operation)
    {
        case OPERATION_READ:
        {
            draw_centered_text((video_height() / 2) - 4, rgb(255, 255, 255), "Reading SRAM and sending it to host...");
            video_display_on_vblank();

            message_send(MESSAGE_SRAM_READ, (void *)SRAM_BASE, SRAM_SIZE);
            break;
        }
        case OPERATION_WRITE:
        {
            draw_centered_text((video_height() / 2) - 4, rgb(255, 255, 255), "Receiving SRAM from host and writing it...");
            video_display_on_vblank();

            int done = 0;
            while (!done)
            {
                uint16_t type = 0;
                uint8_t *data = 0;
                unsigned int length = 0;
                if (message_recv(&type, (void *)&data, &length) == 0)
                {
                    if (type == MESSAGE_SRAM_WRITE && length == SRAM_SIZE)
                    {
                        memcpy((void *)SRAM_BASE, data, SRAM_SIZE);
                        done = 1;
                    }
                    else
                    {
                        printf("Unexpected packet %04X with length %d!\n", type, length);
                    }

                    // Wipe any data that came in.
                    if (data != 0)
                    {
                        free(data);
                    }
                }
            }
            break;
        }
    }

    // Wait for host ack
    while (1)
    {
        uint16_t type = 0;
        uint8_t *data = 0;
        unsigned int length = 0;
        if (message_recv(&type, (void *)&data, &length) == 0)
        {
            if (type == MESSAGE_DONE && length == 0)
            {
                break;
            }
            else
            {
                printf("Unexpected packet %04X with length %d!\n", type, length);
            }

            // Wipe any data that came in.
            if (data != 0)
            {
                free(data);
            }
        }
    }


    // Now, just wait forever...
    draw_centered_text((video_height() / 2) - 4, rgb(0, 255, 0), "Done!");
    video_display_on_vblank();
    while ( 1 ) { ; }
}

void test()
{
    video_init(VIDEO_COLOR_1555);

    while ( 1 )
    {
        // No point in being here at all...
        video_fill_screen(rgb(48, 48, 48));
        draw_centered_text((video_height() / 2) - 4, rgb(255, 255, 255), "Nothing to see here...");
        video_display_on_vblank();
    }
}
