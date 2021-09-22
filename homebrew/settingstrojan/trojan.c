#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "naomi/video.h"
#include "naomi/maple.h"

// We will overwrite this in the final linking script when we are injected
// into a binary. It will point to the original entrypoint that was in the
// binary's header.
uint32_t settings_chunk[4] = {
    0xDDDDDDDD,
    0xAAAAAAAA,
    START_ADDR,
    0xEEEEEEEE,
};

// We will overwrite this as well when we link. It will contain the EEPROM
// contents that we wish to write.
uint8_t eeprom_contents[128] = {
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
};

// We will write to this once we've initialized the EEPROM, so we don't
// mess with settings after somebody goes into the test menu after loading us.
uint32_t *sentinel = (uint32_t *)(START_ADDR - 4);


// Location of the text for debugging.
#define X_LOC 200
#define Y_LOC 200


void main()
{
    // Set up a crude console
    video_init_simple();
    maple_init();

    video_fill_screen(rgb(0, 0, 0));
    video_draw_text(X_LOC, Y_LOC, rgb(255, 255, 255), "Checking settings...");

    // Now, check and see if the sentinel we wrote matches this game. If so, don't
    // bother writing a new EEPROM. This is because somebody might have netbooted
    // and then entered the settings menu.
    uint32_t requested_game;
    memcpy(&requested_game, eeprom_contents + 3, 4);

    if (requested_game != sentinel[0]) {
        video_draw_text(X_LOC, Y_LOC + 12, rgb(255, 255, 255), "Settings need to be written...");

        if(maple_request_eeprom_write(eeprom_contents) == 0)
        {
            video_draw_text(X_LOC, Y_LOC + 24, rgb(0, 255, 0), "Success, your settings are written!");

            // Make sure we don't do this again this power cycle.
            sentinel[0] = requested_game;
        } else {
            video_draw_text(X_LOC, Y_LOC + 24, rgb(255, 0, 0), "Failed, could not write your settings!");
        }
    } else{
        video_draw_text(X_LOC, Y_LOC + 12, rgb(255, 255, 255), "Settings have already been saved!");
    }

    video_wait_for_vblank();
    video_display();

    // Wait ten seconds to display debugging.
    for (int i = 0; i < 60 * 10; i++) {
        video_wait_for_vblank();
    }

    // Boot original code.
    void (*jump_to_exe)() = (void (*))settings_chunk[1];
    jump_to_exe();
}

void test()
{
    // Just a stub in order to compile, we should never call this function since
    // we are never going to overwrite test mode.
}
