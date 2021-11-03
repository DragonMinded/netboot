#include <stdio.h>
#include <stdint.h>
#include <malloc.h>
#include <string.h>
#include "naomi/console.h"
#include "naomi/system.h"
#include "naomi/video.h"
#include "naomi/interrupt.h"

static char *render_buffer = 0;
static unsigned int console_width = 0;
static unsigned int console_height = 0;
static unsigned int console_overscan = 0;
static unsigned int console_visible = 0;

#define TAB_WIDTH 4

#define move_buffer() \
    memmove(render_buffer, render_buffer + (sizeof(char) * console_width), strlen(render_buffer) - (sizeof(char) * console_width)); \
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

    int pos = strlen(render_buffer);

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
    render_buffer[pos] = 0;

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
        console_visible = 1;

        /* Get memory for that size */
        render_buffer = malloc((console_width * console_height) + 1);
        memset(render_buffer, 0, (console_width * console_height) + 1);

        /* Register ourselves with newlib */
        stdio_t console_calls = { 0, __console_write, 0 };
        hook_stdio_calls( &console_calls );
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

        /* Unregister ourselves from newlib */
        stdio_t console_calls = { 0, __console_write, 0 };
        unhook_stdio_calls( &console_calls );
    }
}

void console_render()
{
    if (render_buffer && console_visible)
    {
        /* Ensure data is flushed before rendering */
        fflush( stdout );

        /* Render now */
        char *console_start = render_buffer;
        char *console_end = render_buffer + strlen(render_buffer);
        char *line_buf = malloc(console_width + 1);
        unsigned int line = console_overscan;

        while (console_start != console_end)
        {
            memcpy(line_buf, console_start, console_width);
            line_buf[console_width] = 0;

            video_draw_debug_text(console_overscan, line, rgb(255, 255, 255), line_buf);
            console_start += strlen(line_buf);
            line += 8;
        }

        free(line_buf);
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
        location = render_buffer + strlen(render_buffer);
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
        location[0] = 0;
    }
    irq_restore(old_interrupts);
}
