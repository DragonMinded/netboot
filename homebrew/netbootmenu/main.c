#include <stdint.h>
#include <naomi/video.h>
#include <naomi/ta.h>
#include <naomi/maple.h>
#include <naomi/system.h>
#include <naomi/eeprom.h>
#include <naomi/timer.h>
#include <naomi/audio.h>
#include <naomi/font.h>
#include <naomi/message/message.h>
#include "config.h"
#include "screens.h"

// Sounds compiled in from Makefile.
extern uint8_t *scroll_raw_data;
extern uint8_t *check_raw_data;
extern uint8_t *change_raw_data;
extern unsigned int scroll_raw_len;
extern unsigned int check_raw_len;
extern unsigned int change_raw_len;

// Sprites, compiled in from Makefile.
extern unsigned int up_png_width;
extern unsigned int up_png_height;
extern void *up_png_data;
extern unsigned int dn_png_width;
extern unsigned int dn_png_height;
extern void *dn_png_data;
extern unsigned int cursor_png_width;
extern unsigned int cursor_png_height;
extern void *cursor_png_data;

void main()
{
    // Grab the system configuration
    eeprom_t settings;
    eeprom_read(&settings);

    // Commit the settings again, so we can be sure to obliterate any old ones.
    settings.game.size = 0;
    eeprom_write(&settings);

    // Attach our communication handler for message sending/receiving.
    message_init();

    // Allow printf to go to the host.
    message_stdio_redirect_init();

    // Init the screen for a simple 640x480 framebuffer.
    video_init(VIDEO_COLOR_1555);
    ta_set_background_color(rgb(0, 0, 0));

    // Init audio system for displaying sound effects.
    audio_init();

    // Create global state for the menu.
    state_t state;
    state.settings = &settings;
    state.config = get_config();
    state.test_error_counter = 0.0;

    // Initialize some system sounds.
    state.sounds.scroll = audio_register_sound(AUDIO_FORMAT_16BIT, 44100, scroll_raw_data, scroll_raw_len / 2);
    state.sounds.check = audio_register_sound(AUDIO_FORMAT_16BIT, 44100, check_raw_data, check_raw_len / 2);
    state.sounds.change = audio_register_sound(AUDIO_FORMAT_16BIT, 44100, change_raw_data, change_raw_len / 2);

    // Allow a force override of number of players on the cabinet.
    if (state.config->force_players != 0)
    {
        settings.system.players = state.config->force_players;
    }

    // Attach our fonts
    extern uint8_t *dejavusans_ttf_data;
    extern unsigned int dejavusans_ttf_len;
    state.font_18pt = font_add(dejavusans_ttf_data, dejavusans_ttf_len);
    font_set_size(state.font_18pt, 18);
    state.font_12pt = font_add(dejavusans_ttf_data, dejavusans_ttf_len);
    font_set_size(state.font_12pt, 12);

    // Attach our sprites
    state.sprite_up = ta_texture_desc_malloc_direct(up_png_width, up_png_data, TA_TEXTUREMODE_ARGB1555);
    state.sprite_down = ta_texture_desc_malloc_direct(dn_png_width, dn_png_data, TA_TEXTUREMODE_ARGB1555);
    state.sprite_cursor = ta_texture_desc_malloc_direct(cursor_png_width, cursor_png_data, TA_TEXTUREMODE_ARGB1555);

    // Add fallbacks if they are provided, for rendering CJK or other characters.
    unsigned int fallback_size;
    uint8_t *fallback_data = get_fallback_font(&fallback_size);
    if (fallback_size && fallback_data)
    {
        font_add_fallback(state.font_18pt, fallback_data, fallback_size);
        font_add_fallback(state.font_12pt, fallback_data, fallback_size);
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
        ta_commit_begin();
        draw_screen(&state);
        ta_commit_end();
        ta_render();
        uint32_t draw_time = profile_end(profile);

        // Display some debugging info.
        if (state.config->enable_debug)
        {
            video_draw_debug_text((video_width() / 2) - (18 * 4), video_height() - 16, rgb(0, 200, 255), "FPS: %.01f, %dx%d", fps_value, video_width(), video_height());
            video_draw_debug_text((video_width() / 2) - (18 * 4), video_height() - 24, rgb(0, 200, 255), "uS full draw: %d", draw_time);
        }

        // Actually draw the buffer.
        video_display_on_vblank();

        // Calcualte instantaneous FPS, adjust animation counters.
        uint32_t uspf = profile_end(fps);
        fps_value = (1000000.0 / (double)uspf) + 0.01;
        animation_counter += (double)uspf / 1000000.0;
    }
}

void test()
{
    // Initialize a simple console
    video_init(VIDEO_COLOR_1555);
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
        video_display_on_vblank();
    }
}
