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

// Constants for escape processing.
#define ESCAPE_FLAGS_PROCESSING 0x1
#define ESCAPE_FLAGS_BRACKET 0x2

static char *render_buffer = 0;
static uint16_t *render_attrs = 0;
static unsigned int console_width = 0;
static unsigned int console_height = 0;
static unsigned int console_overscan = 0;
static unsigned int console_visible = 0;
static unsigned int console_pos = 0;
static uint16_t cur_attr = 0;
static uint32_t cur_escape_flags = 0;
static int cur_escape_number = 0;
static int last_escape_numbers[10] = { 0 };
static void *cur_hooks = 0;
static uint16_t saved_attr = 0;
static int saved_pos = 0;

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

        if (cur_escape_flags & ESCAPE_FLAGS_PROCESSING)
        {
            /* Process escape codes */
            int done = 0;
            switch(buf[x])
            {
                case 'c':
                {
                    if (cur_escape_flags & ESCAPE_FLAGS_BRACKET)
                    {
                        /* TODO: If we ever support stdin, report the device code here. */
                    }
                    else
                    {
                        /* Reset all settings to default. */
                        cur_attr = (BLACK) << 4;
                    }
                    done = 1;
                    break;
                }
                case 's':
                {
                    if (cur_escape_flags & ESCAPE_FLAGS_BRACKET)
                    {
                        /* Save cursor position. */
                        saved_pos = pos;
                    }
                    done = 1;
                    break;
                }
                case 'u':
                {
                    if (cur_escape_flags & ESCAPE_FLAGS_BRACKET)
                    {
                        /* Unsave cursor position. */
                        pos = saved_pos;
                    }
                    done = 1;
                    break;
                }
                case 'K':
                {
                    if (cur_escape_flags & ESCAPE_FLAGS_BRACKET)
                    {
                        if (cur_escape_number == -1)
                        {
                            /* Erase after cursor on line. */
                            int col = pos % console_width;
                            if (col < (console_width - 1))
                            {
                                int erasepos = pos;
                                while(erasepos % console_width)
                                {
                                    render_buffer[erasepos + 1] = ' ';
                                    render_attrs[erasepos + 1] = cur_attr;
                                    erasepos ++;
                                }
                            }
                        }
                        if (cur_escape_number == 1)
                        {
                            /* Erase before cursor on line. */
                            int col = pos % console_width;
                            if (col > 0)
                            {
                                int erasepos = pos;
                                while(erasepos % console_width)
                                {
                                    render_buffer[erasepos - 1] = ' ';
                                    render_attrs[erasepos - 1] = cur_attr;
                                    erasepos --;
                                }
                            }
                        }
                        if (cur_escape_number == 2)
                        {
                            /* Erase entire line. */
                            int col = pos % console_width;
                            int erasepos = pos - col;
                            for (int i = 0; i < console_width; i++)
                            {
                                render_buffer[erasepos + i] = ' ';
                                render_attrs[erasepos + i] = cur_attr;
                            }
                        }
                    }
                    done = 1;
                    break;
                }
                case 'J':
                {
                    if (cur_escape_flags & ESCAPE_FLAGS_BRACKET)
                    {
                        if (cur_escape_number == -1)
                        {
                            /* Erase to the end of the screen. */
                            int erasepos = pos + 1;
                            while (erasepos < (console_width * console_height))
                            {
                                render_buffer[erasepos] = ' ';
                                render_attrs[erasepos] = cur_attr;
                                erasepos++;
                            }
                        }
                        if (cur_escape_number == 1)
                        {
                            /* Erase to the beginning of the screen. */
                            int erasepos = pos - 1;
                            while (erasepos >= 0)
                            {
                                render_buffer[erasepos] = ' ';
                                render_attrs[erasepos] = cur_attr;
                                erasepos--;
                            }
                        }
                        if (cur_escape_number == 2)
                        {
                            /* Erase the whole screen, set cursor to home. */
                            for (int erasepos = 0; erasepos < (console_width * console_height); erasepos++)
                            {
                                render_buffer[erasepos] = ' ';
                                render_attrs[erasepos] = cur_attr;
                            }
                            pos = 0;
                        }
                    }
                    done = 1;
                    break;
                }
                case 'n':
                {
                    if (cur_escape_flags & ESCAPE_FLAGS_BRACKET)
                    {
                        switch(cur_escape_number)
                        {
                            case 5:
                                /* TODO: A device status query was requested, send that back if we support stdin. */
                                break;
                            case 6:
                                /* TODO: A cursor position query was requested, send that back if we support stdin. */
                                break;
                        }
                    }
                    done = 1;
                    break;
                }
                case 'h':
                {
                    if (cur_escape_flags & ESCAPE_FLAGS_BRACKET)
                    {
                        switch(cur_escape_number)
                        {
                            case 7:
                                /* TODO: Enable line wrap, we don't support nonwrap mode. */
                                break;
                        }
                    }
                    done = 1;
                    break;
                }
                case 'l':
                {
                    if (cur_escape_flags & ESCAPE_FLAGS_BRACKET)
                    {
                        switch(cur_escape_number)
                        {
                            case 7:
                                /* TODO: Disable line wrap, we don't support nonwrap mode. */
                                break;
                        }
                    }
                    done = 1;
                    break;
                }
                case '(':
                case ')':
                {
                    /* Set default/alternate font, we don't have one so ignore this. */
                    done = 1;
                    break;
                }
                case '[':
                {
                    cur_escape_flags |= ESCAPE_FLAGS_BRACKET;
                    cur_escape_number = -1;
                    for (int i = 0; i < 10; i++)
                    {
                        last_escape_numbers[i] = -1;
                    }
                    break;
                }
                case ';':
                {
                    /* Accumulate numbers for attribute command. */
                    if (cur_escape_flags & ESCAPE_FLAGS_BRACKET)
                    {
                        if (cur_escape_number >= 0)
                        {
                            for (int i = 0; i < 10; i++)
                            {
                                if (last_escape_numbers[i] == -1)
                                {
                                    last_escape_numbers[i] = cur_escape_number;
                                    break;
                                }
                            }

                            cur_escape_number = -1;
                        }
                    }
                    break;
                }
                case 'm':
                {
                    if (cur_escape_flags & ESCAPE_FLAGS_BRACKET)
                    {
                        if (cur_escape_number >= 0)
                        {
                            for (int i = 0; i < 10; i++)
                            {
                                if (last_escape_numbers[i] == -1)
                                {
                                    last_escape_numbers[i] = cur_escape_number;
                                    break;
                                }
                            }

                            cur_escape_number = -1;
                        }

                        for(int i = 0; i < 10; i++)
                        {
                            switch(last_escape_numbers[i])
                            {
                                case 0:
                                    /* Reset settings to default. */
                                    cur_attr = (BLACK) << 4;
                                    break;
                                case 1:
                                    /* Bright mode */
                                    cur_attr &= (~DIM);
                                    cur_attr &= (~(DIM << 4));
                                    break;
                                case 2:
                                    /* Dim mode */
                                    cur_attr |= DIM;
                                    cur_attr |= (DIM << 4);
                                    break;
                                case 4:
                                    /* Underscore */
                                    cur_attr |= UNDERSCORE;
                                    break;
                                case 5:
                                    /* Don't support blink (yet?) */
                                    break;
                                case 7:
                                    /* Reverse mode */
                                    cur_attr |= REVERSE;
                                    break;
                                case 8:
                                    /* Hidden text, don't support it (yet?) */
                                    break;
                                case 30:
                                    /* FG color black */
                                    cur_attr = (cur_attr & 0xFFFFFFF8) | BLACK;
                                    break;
                                case 31:
                                    /* FG color red */
                                    cur_attr = (cur_attr & 0xFFFFFFF8) | RED;
                                    break;
                                case 32:
                                    /* FG color green */
                                    cur_attr = (cur_attr & 0xFFFFFFF8) | GREEN;
                                    break;
                                case 33:
                                    /* FG color yellow */
                                    cur_attr = (cur_attr & 0xFFFFFFF8) | YELLOW;
                                    break;
                                case 34:
                                    /* FG color blue */
                                    cur_attr = (cur_attr & 0xFFFFFFF8) | BLUE;
                                    break;
                                case 35:
                                    /* FG color magenta */
                                    cur_attr = (cur_attr & 0xFFFFFFF8) | MAGENTA;
                                    break;
                                case 36:
                                    /* FG color cyan */
                                    cur_attr = (cur_attr & 0xFFFFFFF8) | CYAN;
                                    break;
                                case 37:
                                    /* FG color white */
                                    cur_attr = (cur_attr & 0xFFFFFFF8) | WHITE;
                                    break;
                                case 40:
                                    /* BG color black */
                                    cur_attr = (cur_attr & 0xFFFFFF8F) | (BLACK << 4);
                                    break;
                                case 41:
                                    /* BG color red */
                                    cur_attr = (cur_attr & 0xFFFFFF8F) | (RED << 4);
                                    break;
                                case 42:
                                    /* BG color green */
                                    cur_attr = (cur_attr & 0xFFFFFF8F) | (GREEN << 4);
                                    break;
                                case 43:
                                    /* BG color yellow */
                                    cur_attr = (cur_attr & 0xFFFFFF8F) | (YELLOW << 4);
                                    break;
                                case 44:
                                    /* BG color blue */
                                    cur_attr = (cur_attr & 0xFFFFFF8F) | (BLUE << 4);
                                    break;
                                case 45:
                                    /* BG color magenta */
                                    cur_attr = (cur_attr & 0xFFFFFF8F) | (MAGENTA << 4);
                                    break;
                                case 46:
                                    /* BG color cyan */
                                    cur_attr = (cur_attr & 0xFFFFFF8F) | (CYAN << 4);
                                    break;
                                case 47:
                                    /* BG color white */
                                    cur_attr = (cur_attr & 0xFFFFFF8F) | (WHITE << 4);
                                    break;
                            }
                        }
                    }

                    done = 1;
                    break;
                }
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                {
                    if (cur_escape_flags & ESCAPE_FLAGS_BRACKET)
                    {
                        int number = buf[x] - '0';
                        if (cur_escape_number == -1)
                        {
                            cur_escape_number = number;
                        }
                        else
                        {
                            cur_escape_number = (cur_escape_number * 10) + number;
                        }
                    }
                    else
                    {
                        switch(buf[x])
                        {
                            case '7':
                            {
                                /* Save cursor and attrs. */
                                saved_attr = cur_attr;
                                saved_pos = pos;
                                break;
                            }
                            case '8':
                            {
                                /* Restore cursor and attrs. */
                                cur_attr = saved_attr;
                                pos = saved_pos;
                                break;
                            }
                        }

                        done = 1;
                    }
                    break;
                }
                default:
                {
                    /* Unrecognized, bail out. */
                    done = 1;
                    break;
                }
            }

            if (done)
            {
                cur_escape_flags = 0;
            }
        }
        else
        {
            switch(buf[x])
            {
                case '\r':
                {
                    /* Go to beginning of the line. */
                    int col = pos % console_width;
                    pos -= col;
                    break;
                }
                case '\n':
                {
                    /* Go to next line. */
                    int col = pos % console_width;
                    pos += console_width - col;
                    break;
                }
                case '\t':
                {
                    /* Add enough spaces to go to the next tab stop */
                    if(!(pos % TAB_WIDTH))
                    {
                        render_buffer[pos] = ' ';
                        render_attrs[pos] = cur_attr;
                        pos++;
                    }

                    while(pos % TAB_WIDTH)
                    {
                        render_buffer[pos] = ' ';
                        render_attrs[pos] = cur_attr;
                        pos++;
                    }
                    break;
                }
                case 0x1B:
                {
                    /* Escape sequence start */
                    cur_escape_flags = ESCAPE_FLAGS_PROCESSING;
                    cur_escape_number = -1;
                    for (int i = 0; i < 10; i++)
                    {
                        last_escape_numbers[i] = -1;
                    }
                    break;
                }
                default:
                {
                    /* Copy character over */
                    render_buffer[pos] = buf[x];
                    render_attrs[pos] = cur_attr;
                    pos++;
                    break;
                }
            }
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
        memset(render_attrs, (BLACK) << 4, (console_width * console_height) * sizeof(render_attrs[0]));
        cur_attr = (BLACK) << 4;
        cur_escape_flags = 0;
        saved_attr = cur_attr;
        saved_pos = console_pos;

        /* Register ourselves with newlib */
        stdio_t console_calls = { 0, __console_write, 0 };
        cur_hooks = hook_stdio_calls( &console_calls );
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
        cur_attr = 0;
        cur_escape_flags = 0;

        /* Unregister ourselves from newlib */
        unhook_stdio_calls( cur_hooks );
    }
}

color_t attr_to_color(uint8_t attr)
{
    switch(attr & 0xF)
    {
        case WHITE:
            return rgb(255, 255, 255);
        case BLACK:
            return rgb(0, 0, 0);
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
        color_t black = rgb(0, 0, 0);
        for (int pos = 0; pos < console_width * console_height; pos++)
        {
            uint16_t render_attr = render_attrs[pos];
            color_t bgcolor;
            color_t fgcolor;

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

            if (bgcolor.r != black.r && bgcolor.g != black.g && bgcolor.b != black.b)
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
