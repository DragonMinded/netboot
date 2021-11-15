#include <stdio.h>
#include <stdint.h>
#include <malloc.h>
#include <string.h>
#include "naomi/console.h"
#include "naomi/system.h"
#include "naomi/video.h"
#include "naomi/interrupt.h"
#include "irqinternal.h"

static char *render_buffer = 0;
static unsigned int console_width = 0;
static unsigned int console_height = 0;
static unsigned int console_overscan = 0;
static unsigned int console_visible = 0;
static unsigned int console_pos = 0;
static void *curhooks = 0;

#define TAB_WIDTH 4

#define move_buffer() \
    memmove(render_buffer, render_buffer + (sizeof(char) * console_width), (console_width * (console_height - 1))); \
    pos -= console_width;

static int __console_write( const char * const buf, unsigned int len )
{
    uint32_t old_interrupts = irq_disable();

    if (!render_buffer || !console_width || !console_height)
    {
        // The console is not initialized.
        irq_restore(old_interrupts);
        return len;
    }

    int pos = console_pos;

    /* Copy over to screen buffer */
    for(int x = 0; x < len; x++)
    {
        if(pos == console_width * console_height)
        {
            /* Need to scroll the buffer */
            move_buffer();
        }

        switch(buf[x])
        {
            case '\r':
            case '\n':
                /* Add enough space to get to next line */
                if(!(pos % console_width))
                {
                    render_buffer[pos++] = ' ';
                }

                while(pos % console_width)
                {
                    render_buffer[pos++] = ' ';
                }
                break;
            case '\t':
                /* Add enough spaces to go to the next tab stop */
                if(!(pos % TAB_WIDTH))
                {
                    render_buffer[pos++] = ' ';
                }

                while(pos % TAB_WIDTH)
                {
                    render_buffer[pos++] = ' ';
                }
                break;
            default:
                /* Copy character over */
                render_buffer[pos++] = buf[x];
                break;
        }
    }

    /* Cap off the end! */
    console_pos = pos;

    /* Always write all */
    irq_restore(old_interrupts);
    return len;
}

void console_init(unsigned int overscan)
{
    if (!render_buffer)
    {
        /* Calculate size of the console */
        console_width = (video_width() - (overscan * 2)) / 8;
        console_height = (video_height() - (overscan * 2)) / 8;
        console_overscan = overscan;
        console_pos = 0;
        console_visible = 1;

        /* Get memory for that size */
        render_buffer = malloc((console_width * console_height));
        if (render_buffer == 0)
        {
            _irq_display_invariant("malloc failure", "failed to allocate memory for console!");
        }
        memset(render_buffer, ' ', (console_width * console_height));

        /* Register ourselves with newlib */
        stdio_t console_calls = { 0, __console_write, 0 };
        curhooks = hook_stdio_calls( &console_calls );
    }
}

void console_free()
{
    if (render_buffer)
    {
        /* Nuke the console buffer */
        free(render_buffer);

        render_buffer = 0;
        console_width = 0;
        console_height = 0;
        console_pos = 0;

        /* Unregister ourselves from newlib */
        unhook_stdio_calls( curhooks );
    }
}

void console_render()
{
    if (render_buffer && console_visible)
    {
        /* Ensure data is flushed before rendering */
        fflush( stdout );

        /* Render now */
        for (int pos = 0; pos < console_width * console_height; pos++)
        {
            video_draw_debug_character(console_overscan + ((pos % console_width) * 8), console_overscan + ((pos / console_width) * 8), rgb(255, 255, 255), render_buffer[pos]);
        }
    }
}

void console_set_visible(unsigned int visibility)
{
    console_visible = visibility;
}

char * console_save()
{
    uint32_t old_interrupts = irq_disable();
    char *location = 0;
    if (render_buffer)
    {
        // Return where we are in the console, so it can be restored later.
        location = render_buffer + console_pos;
    }

    irq_restore(old_interrupts);
    return location;
}

void console_restore(char *location)
{
    uint32_t old_interrupts = irq_disable();
    if (render_buffer && location >= render_buffer && location < (render_buffer + (console_width * console_height)))
    {
        // If the pointer is within our console, cap the console off at that location.
        console_pos = (int)(location - render_buffer);
        memset(render_buffer + console_pos, ' ', (console_width * console_height) - console_pos);
    }
    irq_restore(old_interrupts);
}
