#include <stdio.h>
#include <stdint.h>
#include "naomi/video.h"

void main()
{
    video_init_simple();

    // Draw a few simple things on the screen.
    video_fill_screen(rgbto565(48, 48, 48));
    video_fill_box(20, 20, 100, 100, rgbto565(0, 0, 0));
    video_draw_line(20, 20, 100, 100, rgbto565(0, 255, 0));
    video_draw_line(100, 20, 20, 100, rgbto565(0, 255, 0));
    video_draw_line(20, 20, 100, 20, rgbto565(0, 255, 0));
    video_draw_line(20, 20, 20, 100, rgbto565(0, 255, 0));
    video_draw_line(100, 20, 100, 100, rgbto565(0, 255, 0));
    video_draw_line(20, 100, 100, 100, rgbto565(0, 255, 0));
    video_draw_text(20, 180, rgbto565(255, 255, 255), "Hello, world!");
    video_draw_text(20, 200, rgbto565(255, 0, 255), "This is a test...");

    // Display a liveness counter that goes up 60 times a second.
    char buffer[64];
    unsigned int counter = 0;
    while ( 1 )
    {
        sprintf(buffer, "Aliveness counter: %d", counter++);

        video_fill_box(20, 220, 20 + (8*30), 220 + 8, rgbto565(48, 48, 48));
        video_draw_text(20, 220, rgbto565(200, 200, 20), buffer);
        video_wait_for_vblank();
        video_display();
    }
}

void test()
{
    video_init_simple();

    video_fill_screen(rgbto565(48, 48, 48));
    video_draw_text(320 - 56, 236, rgbto565(255, 255, 255), "test mode stub");
    video_wait_for_vblank();
    video_display();

    while ( 1 ) { ; }
}
