#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <naomi/video.h>
#include <naomi/cart.h>
#include <naomi/romfs.h>

void main()
{
    // We just want a simple framebuffer display.
    video_init_simple();
    video_set_background_color(rgb(48, 48, 48));

    // Attach to the built-in ROM FS so we can read some simple files.
    romfs_init_default();

    // Read the cart header, parse out some fun bits.
    uint8_t header[HEADER_SIZE];
    cart_read_rom_header(header);

    char name[33] = { 0 };
    char publisher[33] = { 0 };
    memcpy(name, header + 0x50, 32);
    memcpy(publisher, header + 0x10, 32);

    // They have spaces until the end of them, so trim those off.
    int i = 31;
    while (name[i] == ' ' && i >= 0)
    {
        name[i] = 0;
        i--;
    }

    i = 31;
    while (publisher[i] == ' ' && i >= 0)
    {
        publisher[i] = 0;
        i--;
    }

    // Read a file out of the ROM FS.
    char filedata[256];
    memset(filedata, 0, 256);

    FILE * fp = fopen("rom://test.txt", "r");
    if (fp)
    {
        // Leave room for a final null termination.
        fread(filedata, 1, 255, fp);
        fclose(fp);
    }
    else
    {
        sprintf(filedata, "File read error: %s (%d)", strerror(errno), errno);
    }

    unsigned int counter = 0;
    while ( 1 )
    {
        // Draw a few simple things on the screen.
        video_draw_debug_text(48, 180, rgb(255, 255, 255), "Cartridge header read, my name is \"%s\"!", name);
        video_draw_debug_text(48, 200, rgb(255, 255, 255), "I was published by %s!", publisher);
        video_draw_debug_text(48, 220, rgb(255, 255, 255), filedata);

        // Display a liveness counter that goes up 60 times a second.
        video_draw_debug_text(48, 300, rgb(200, 200, 20), "Aliveness counter: %d", counter++);

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
