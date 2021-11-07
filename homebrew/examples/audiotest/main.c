#include <stdio.h>
#include <stdint.h>
#include "naomi/video.h"
#include "naomi/audio.h"
#include "naomi/system.h"

// Our sound, as linked by our makefile.
extern uint8_t *success_raw_data;
extern unsigned int success_raw_len;

void main()
{
    video_init_simple();

    // Display status, since loading the binary can take awhile.
    video_fill_screen(rgb(48, 48, 48));
    video_draw_debug_text(20, 20, rgb(255, 255, 255), "Loading AICA binary...");
    video_display_on_vblank();

    // Initialize audio system.
    audio_init();

    // Request a sound be played immediately.
    audio_play_sound(AUDIO_FORMAT_8BIT, 44100, SPEAKER_LEFT | SPEAKER_RIGHT, success_raw_data, success_raw_len);

    unsigned int counter = 0;
    while ( 1 )
    {
        // Draw a few simple things on the screen.
        video_fill_screen(rgb(48, 48, 48));

        // Display a liveness counter that goes up 60 times a second.
        video_draw_debug_text(
            20,
            20,
            rgb(200, 200, 20),
            "Aliveness counter: %d (%lu)",
            counter++,
            audio_aica_uptime()
        );
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
