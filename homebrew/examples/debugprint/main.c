#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <naomi/video.h>
#include <naomi/maple.h>
#include <naomi/message/message.h>

void main()
{
    // We just want a simple framebuffer display.
    video_init(VIDEO_COLOR_1555);
    video_set_background_color(rgb(48, 48, 48));

    // Initialize message library, ask for stdio hooks.
    message_init();
    message_stdio_redirect_init();

    uint32_t counter = 0;
    while ( 1 )
    {
        // Figure out if we need to printf/fprintf.
        maple_poll_buttons();
        jvs_buttons_t pressed = maple_buttons_pressed();
        if (pressed.player1.start || pressed.player2.start)
        {
            printf("Hello, world from Naomi!\n");
            fprintf(stderr, "Hello, stderr from Naomi!\n");
        }

        // Draw a few simple things on the screen.
        video_draw_debug_text(100, 180, rgb(255, 255, 255), "Net Dimm message library test stub.");
        video_draw_debug_text(100, 200, rgb(255, 0, 255), "Press start to send a stdout and stderr message to host.");

        // Display a liveness counter that goes up 60 times a second.
        video_draw_debug_text(100, 260, rgb(200, 200, 20), "Aliveness counter: %d", counter++);

        // Actually draw the framebuffer.
        video_display_on_vblank();
    }
}

void test()
{
    video_init(VIDEO_COLOR_1555);

    while ( 1 )
    {
        video_fill_screen(rgb(48, 48, 48));
        video_draw_debug_text(320 - 56, 236, rgb(255, 255, 255), "test mode stub");
        video_display_on_vblank();
    }
}
