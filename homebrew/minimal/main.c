#include <stdio.h>
#include <stdint.h>
#include <naomi/video.h>

void main()
{
    video_init_simple();
    video_set_background_color(rgb(48, 48, 48));

    while ( 1 )
    {
        video_draw_debug_text((video_width() / 2) - (8 * 7), video_height() / 2, rgb(255, 255, 255), "Hello, world!");
        video_display_on_vblank();
    }
}

void test()
{
    video_init_simple();
    video_set_background_color(rgb(48, 48, 48));

    while ( 1 )
    {
        video_draw_debug_text((video_width() / 2) - (8 * 7), video_height() / 2, rgb(255, 255, 255), "Hello, test!");
        video_display_on_vblank();
    }
}
