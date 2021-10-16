#include <stdio.h>
#include <stdint.h>
#include "naomi/video.h"
#include "naomi/timer.h"

void main()
{
    video_init_simple();

    char buffer[64];
    unsigned int counter = 0;
    double fps_value = 0.0;
    while ( 1 )
    {
        // Grab a few profilers so we can see the performance of this code.
        int fps = profile_start();
        int draw_time = profile_start();

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
        sprintf(buffer, "Draw Time in uS: %d", profile_end(draw_time));
        video_draw_text(20, 240, rgb(200, 200, 20), buffer);
        sprintf(buffer, "FPS: %.01f, %dx%d", fps_value, video_width(), video_height());
        video_draw_text(20, 260, rgb(200, 200, 20), buffer);

        video_wait_for_vblank();
        video_display();

        // Calculate instantaneous FPS.
        uint32_t uspf = profile_end(fps);
        fps_value = 1000000.0 / (double)uspf;
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
