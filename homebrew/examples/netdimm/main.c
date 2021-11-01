#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "naomi/video.h"
#include "naomi/dimmcomms.h"

uint32_t last_read_addr = 0;
uint32_t last_read_length = 0;
uint32_t last_read_data = 0;
uint32_t last_write_addr = 0;
uint32_t last_write_length = 0;
uint32_t last_write_data = 0;

uint32_t peek_memory(unsigned int address, int size)
{
    static uint32_t memval = 0x12345678;

    if (size == 1)
    {
        last_read_addr = address;
        last_read_length = size;
        last_read_data = memval & 0xFF;
        memval++;
        return last_read_data;
    }
    if (size == 2)
    {
        last_read_addr = address;
        last_read_length = size;
        last_read_data = memval & 0xFFFF;
        memval++;
        return last_read_data;
    }
    if (size == 4)
    {
        last_read_addr = address;
        last_read_length = size;
        last_read_data = memval;
        memval++;
        return last_read_data;
    }

    return 0;
}

void poke_memory(unsigned int address, int size, uint32_t data)
{
    if (size == 1 || size == 2 || size == 4)
    {
        last_write_addr = address;
        last_write_data = data;
        last_write_length = size;
    }
}

void main()
{
    // We just want a simple framebuffer display.
    video_init_simple();
    video_set_background_color(rgb(48, 48, 48));

    unsigned int counter = 0;

    // Attach our handler so we can see what the net dimm is doing.
    dimm_comms_attach_hooks(&peek_memory, &poke_memory);

    while ( 1 )
    {
        // Draw a few simple things on the screen.
        video_draw_debug_text(100, 180, rgb(255, 255, 255), "Net Dimm communications test stub.");
        video_draw_debug_text(100, 200, rgb(255, 0, 255), "Use the peek/poke commands to talk to this code!");

        // Display the last read/write that was executed.
        if (last_read_length == 1) {
            video_draw_debug_text(100, 220, rgb(0, 255, 0), "Last read: %08X (1 byte: %02X)", last_read_addr, last_read_data & 0xFF);
        } else if (last_read_length == 2) {
            video_draw_debug_text(100, 220, rgb(0, 255, 0), "Last read: %08X (2 bytes: %02X)", last_read_addr, last_read_data & 0xFFFF);
        } else if (last_read_length == 4) {
            video_draw_debug_text(100, 220, rgb(0, 255, 0), "Last read: %08X (4 bytes: %02X)", last_read_addr, last_read_data);
        } else {
            video_draw_debug_text(100, 220, rgb(0, 255, 0), "No reads...");
        }

        if (last_write_length == 1) {
            video_draw_debug_text(100, 240, rgb(0, 255, 0), "Last write: %08X = %02X", last_write_addr, last_write_data & 0xFF);
        } else if (last_write_length == 2) {
            video_draw_debug_text(100, 240, rgb(0, 255, 0), "Last write: %08X = %04X", last_write_addr, last_write_data & 0xFFFF);
        } else if (last_write_length == 4) {
            video_draw_debug_text(100, 240, rgb(0, 255, 0), "Last write: %08X = %08X", last_write_addr, last_write_data);
        } else {
            video_draw_debug_text(100, 240, rgb(0, 255, 0), "No writes...");
        }

        // Display a liveness counter that goes up 60 times a second.
        video_draw_debug_text(100, 260, rgb(200, 200, 20), "Aliveness counter: %d", counter++);

        // Actually draw the framebuffer.
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
