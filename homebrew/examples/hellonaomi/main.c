#include <stdio.h>
#include <stdint.h>
#include "naomi/video.h"

void main()
{
    video_init_simple();

    char buffer[64];
    unsigned int counter = 0;
    while ( 1 )
    {
        // Draw a few simple things on the screen.
        video_fill_screen(rgb(48, 48, 48));
        video_fill_box(20, 20, 100, 100, rgb(0, 0, 0));
        video_draw_line(20, 20, 100, 100, rgb(0, 255, 0));
        video_draw_line(100, 20, 20, 100, rgb(0, 255, 0));
        video_draw_line(20, 20, 100, 20, rgb(0, 255, 0));
        video_draw_line(20, 20, 20, 100, rgb(0, 255, 0));
        video_draw_line(100, 20, 100, 100, rgb(0, 255, 0));
        video_draw_line(20, 100, 100, 100, rgb(0, 255, 0));
        video_draw_text(20, 180, rgb(255, 255, 255), "Hello, world!");
        video_draw_text(20, 200, rgb(255, 0, 255), "This is a test...");

        // Display a liveness counter that goes up 60 times a second.
        sprintf(buffer, "Aliveness counter: %d", counter++);
        video_draw_text(20, 220, rgb(200, 200, 20), buffer);
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
