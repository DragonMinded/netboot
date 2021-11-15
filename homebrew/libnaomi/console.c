#include <stdio.h>
#include <stdint.h>
#include <malloc.h>
#include <string.h>
#include "naomi/console.h"
#include "naomi/system.h"
#include "naomi/video.h"
#include "naomi/interrupt.h"
#include "irqinternal.h"

// Constants for the lower two nibbles of render_attrs.
#define WHITE 0x0
#define BLACK 0x1
#define RED 0x2
#define GREEN 0x3
#define YELLOW 0x4
#define BLUE 0x5
#define MAGENTA 0x6
#define CYAN 0x7

#define DIM 0x8

// Constants for the upper byte of render_attrs.
#define REVERSE 0x100
#define UNDERSCORE 0x200

static char *render_buffer = 0;
static uint16_t *render_attrs = 0;
static unsigned int console_width = 0;
static unsigned int console_height = 0;
static unsigned int console_overscan = 0;
static unsigned int console_visible = 0;
static unsigned int console_pos = 0;
static void *curhooks = 0;

#define TAB_WIDTH 4

#define move_buffer() \
    memmove(render_buffer, render_buffer + console_width, (console_width * (console_height - 1))); \
    memmove(render_attrs, render_attrs + console_width, sizeof(render_attrs[0]) * (console_width * (console_height - 1))); \
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
    if (!render_buffer || !render_attrs)
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

        /* Get memory for render attributes */
        render_attrs = malloc((console_width * console_height) * sizeof(render_attrs[0]));
        if (render_attrs == 0)
        {
            _irq_display_invariant("malloc failure", "failed to allocate memory for console attributes!");
        }
        memset(render_attrs, (BLACK | DIM) << 4, (console_width * console_height) * sizeof(render_attrs[0]));

        /* Register ourselves with newlib */
        stdio_t console_calls = { 0, __console_write, 0 };
        curhooks = hook_stdio_calls( &console_calls );
    }
}

void console_free()
{
    if (render_buffer && render_attrs)
    {
        /* Nuke the console buffer */
        free(render_buffer);
        free(render_attrs);

        render_buffer = 0;
        render_attrs = 0;
        console_width = 0;
        console_height = 0;
        console_pos = 0;

        /* Unregister ourselves from newlib */
        unhook_stdio_calls( curhooks );
    }
}

uint32_t attr_to_color(uint8_t attr)
{
    switch(attr & 0xF)
    {
        case WHITE:
            return rgb(255, 255, 255);
        case BLACK:
            return rgb(85, 87, 83);
        case RED:
            return rgb(239, 41, 41);
        case GREEN:
            return rgb(138, 226, 52);
        case YELLOW:
            return rgb(252, 233, 79);
        case BLUE:
            return rgb(50, 175, 255);
        case MAGENTA:
            return rgb(173, 127, 168);
        case CYAN:
            return rgb(52, 226, 226);
        case WHITE | DIM:
            return rgb(211, 215, 207);
        case BLACK | DIM:
            return rgb(0, 0, 0);
        case RED | DIM:
            return rgb(204, 0, 0);
        case GREEN | DIM:
            return rgb(78, 154, 6);
        case YELLOW | DIM:
            return rgb(196, 160, 0);
        case BLUE | DIM:
            return rgb(114, 159, 207);
        case MAGENTA | DIM:
            return rgb(117, 80, 123);
        case CYAN | DIM:
            return rgb(6, 152, 154);
    }

    // Should never happen.
    return rgb(255, 255, 255);
}

void console_render()
{
    if (render_buffer && render_attrs && console_visible)
    {
        /* Ensure data is flushed before rendering */
        fflush( stdout );

        /* Render now */
        uint32_t black = rgb(0, 0, 0);
        for (int pos = 0; pos < console_width * console_height; pos++)
        {
            uint16_t render_attr = render_attrs[pos];
            uint32_t bgcolor;
            uint32_t fgcolor;

            if (render_attr & REVERSE)
            {
                fgcolor = attr_to_color(render_attr >> 4);
                bgcolor = attr_to_color(render_attr);
            }
            else
            {
                bgcolor = attr_to_color(render_attr >> 4);
                fgcolor = attr_to_color(render_attr);
            }

            if (bgcolor != black)
            {
                // Only draw background if it is not black (our transparent color).
                video_fill_box(
                    console_overscan + ((pos % console_width) * 8),
                    console_overscan + ((pos / console_width) * 8),
                    console_overscan + ((pos % console_width) * 8) + 8,
                    console_overscan + ((pos / console_width) * 8) + 8,
                    bgcolor
                );
            }

            if (render_buffer[pos] > 0x20 && render_buffer[pos] < 0x80)
            {
                // Only draw displayable characters.
                video_draw_debug_character(
                    console_overscan + ((pos % console_width) * 8),
                    console_overscan + ((pos / console_width) * 8),
                    fgcolor,
                    render_buffer[pos]
                );
            }

            if (render_attr & UNDERSCORE)
            {
                video_draw_line(
                    console_overscan + ((pos % console_width) * 8),
                    console_overscan + ((pos / console_width) * 8) + 8,
                    console_overscan + ((pos % console_width) * 8) + 8,
                    console_overscan + ((pos / console_width) * 8) + 8,
                    fgcolor
                );
            }
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
        memset(render_attrs + console_pos, ((BLACK | DIM) << 4), ((console_width * console_height) - console_pos) * sizeof(render_attrs[0]));
    }
    irq_restore(old_interrupts);
}
