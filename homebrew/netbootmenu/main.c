#include <stdint.h>
#include "naomi/video.h"
#include "naomi/maple.h"
#include "naomi/system.h"
#include "naomi/eeprom.h"
#include "naomi/timer.h"
#include "config.h"
#include "packet.h"
#include "screens.h"

void main()
{
    // Grab the system configuration
    eeprom_t settings;
    eeprom_read(&settings);

    // Attach our communication handler for packet sending/receiving.
    packetlib_init();

    // Init the screen for a simple 640x480 framebuffer.
    video_init_simple();
    video_set_background_color(rgb(0, 0, 0));

    // Create global state for the menu.
    state_t state;
    state.settings = &settings;
    state.config = get_config();
    state.test_error_counter = 0.0;

    // Allow a force override of number of players on the cabinet.
    if (state.config->force_players != 0)
    {
        settings.system.players = state.config->force_players;
    }

    // Attach our fonts
    extern uint8_t *dejavusans_ttf_data;
    extern unsigned int dejavusans_ttf_len;
    state.font_18pt = video_font_add(dejavusans_ttf_data, dejavusans_ttf_len);
    video_font_set_size(state.font_18pt, 18);
    state.font_12pt = video_font_add(dejavusans_ttf_data, dejavusans_ttf_len);
    video_font_set_size(state.font_12pt, 12);

    // Add fallbacks if they are provided, for rendering CJK or other characters.
    unsigned int fallback_size;
    uint8_t *fallback_data = get_fallback_font(&fallback_size);
    if (fallback_size && fallback_data)
    {
        video_font_add_fallback(state.font_18pt, fallback_data, fallback_size);
        video_font_add_fallback(state.font_12pt, fallback_data, fallback_size);
    }

    // FPS calculation for debugging.
    double fps_value = 60.0;

    // Simple animations for the screen.
    double animation_counter = 0.0;

    while ( 1 )
    {
        // Get FPS measurements.
        int fps = profile_start();

        // Set up the global state for any draw screen.
        state.fps = fps_value;
        state.animation_counter = animation_counter;

        // Now, draw the current screen.
        int profile = profile_start();
        draw_screen(&state);
        uint32_t draw_time = profile_end(profile);

        // Display some debugging info.
        if (state.config->enable_debug)
        {
            video_draw_debug_text((video_width() / 2) - (18 * 4), video_height() - 16, rgb(0, 200, 255), "FPS: %.01f, %dx%d", fps_value, video_width(), video_height());
            video_draw_debug_text((video_width() / 2) - (18 * 4), video_height() - 24, rgb(0, 200, 255), "uS full draw: %d", draw_time);
        }

        // Actually draw the buffer.
        video_wait_for_vblank();
        video_display();

        // Calcualte instantaneous FPS, adjust animation counters.
        uint32_t uspf = profile_end(fps);
        fps_value = (1000000.0 / (double)uspf) + 0.01;
        animation_counter += (double)uspf / 1000000.0;
    }
}

void test()
{
    // Initialize a simple console
    video_init_simple();
    video_set_background_color(rgb(0, 0, 0));

    while ( 1 )
    {
        // First, poll the buttons and act accordingly.
        maple_poll_buttons();
        jvs_buttons_t buttons = maple_buttons_pressed();

        if (buttons.psw1 || buttons.test)
        {
            // Request to go into system test mode.
            enter_test_mode();
        }

        // It would not make sense to have a test menu for our ROM. This is
        // because all of our settings are saved on the controlling PC or
        // Raspberry PI so that it can survive booting games and having the
        // EEPROM cleared every boot. So, nothing is worth changing here.
        video_draw_debug_text(
            (video_width() / 2) - (8 * (56 / 2)),
            (video_height() / 2) - (8 * 4),
            rgb(255, 255, 255),
            "No game settings available here. To change settings for\n"
            "the menu, press [test] when you are on the main screen.\n\n"
            "                  press [test] to exit                  "
        );
        video_wait_for_vblank();
        video_display();
    }
}
