#include <memory.h>
#include <stdint.h>
#include <stdlib.h>
#include "naomi/video.h"
#include "naomi/system.h"
#include "font.h"


// TODO: Need to support more than 640x480 framebuffer mode.
// TODO: Need to support more than RGB565 color.
// TODO: Need to support vertical orientation.


static void *buffer_base = 0;
static int buffer_loc = 0;


void video_wait_for_vblank()
{
    volatile unsigned int *videobase = (volatile unsigned int *)POWERVR2_BASE;

    while(!(videobase[POWERVR2_SYNC_STAT] & 0x01ff)) { ; }
    while((videobase[POWERVR2_SYNC_STAT] & 0x01ff)) { ; }
}

unsigned int video_width()
{
    // Hardcoded for now
    return 640;
}

unsigned int video_height()
{
    // Hardcoded for now
    return 480;
}

unsigned int video_depth()
{
    // Hardcoded for now
    return 2;
}

// TODO: This function assumes 640x480 VGA, we should support more varied options.
void video_init_simple()
{
    volatile unsigned int *videobase = (volatile unsigned int *)POWERVR2_BASE;

    // Set up video timings copied from Naomi BIOS.
    videobase[POWERVR2_VRAM_CFG3] = 0x15D1C955;
    videobase[POWERVR2_VRAM_CFG1] = 0x00000020;

    // Reset video.
    videobase[POWERVR2_RESET] = 0;

    // Set border color to black.
    videobase[POWERVR2_BORDER_COL] = 0;

    // Don't display border across whole screen.
    videobase[POWERVR2_VIDEO_CFG] = 0x00160000;

    // Set up frameebuffer config to enable display, set pixel mode, no line double.
    videobase[POWERVR2_FB_DISPLAY_CFG] = (
        0x1 << 23 |  // Double pixel clock for VGA.
        0x1 << 2 |   // RGB565 mode.
        0x1 << 0     // Enable display.
    );

    // Set up framebuffer render config to dither enabled, RGB565, no alpha threshold.
    videobase[POWERVR2_FB_RENDER_CFG] = (
        0x1 << 3 |  // Dither enabled.
        0x1 << 0    // RGB565 mode.
    );

    // Set up even/odd field video base address, shifted by bpp.
    video_display();

    // Set up render modulo, (bpp * width) / 8.
    videobase[POWERVR2_FB_RENDER_MODULO] = (2 * 640) / 8;

    // Set up vertical position.
    videobase[POWERVR2_VPOS] = (
        35 << 16 |  // Even position.
        35 << 0     // Odd position.
    );
    videobase[POWERVR2_VBORDER] = (
        40 << 16 |  // Start.
        (480 + 40) << 0    // End.
    );

    // Set up horizontal position.
    videobase[POWERVR2_HPOS] = 166;

    // Set up refresh rate.
    videobase[POWERVR2_SYNC_LOAD] = (
        524 << 16  |  // Vsync
        857 << 0      // Hsync
    );

    // Set up display size.
    videobase[POWERVR2_FB_DISPLAY_SIZE] = (
        1 << 20 |                   // Interlace skip modulo if we are interlaced ((width / 4) * bpp) + 1
        (480 - 1) << 10 |           // height - 1
        (((640 / 4) * 2) - 1) << 0  // ((width / 4) * bpp) - 1
    );

    // Enable display
    videobase[POWERVR2_SYNC_CFG] = (
        1 << 8 |  // Enable video
        0 << 6 |  // VGA mode
        0 << 4 |  // Non-interlace
        0 << 2 |  // Negative H-sync
        0 << 1    // Negative V-sync
    );

    // Set up horizontal clipping to clip within 0-640.
    videobase[POWERVR2_FB_CLIP_X] = (640 << 16) | (0 << 0);

    // Set up vertical clipping to within 0-480.
    videobase[POWERVR2_FB_CLIP_Y] = (480 << 16) | (0 << 0);

    // Wait for vblank like games do.
    video_wait_for_vblank();
}

uint32_t rgb(unsigned int r, unsigned int g, unsigned int b)
{
    if(video_depth() == 2)
    {
        r = (r >> 3) & 0x1F;
        g = (g >> 2) & 0x3F;
        b = (b >> 3) & 0x1F;

        return b | (g << 5) | (r << 11);
    }
    else
    {
        // TODO
        return 0;
    }
}

void video_fill_screen(uint32_t color)
{
    if(video_depth() == 2)
    {
        /* 16bpp colors, double our fill speed */
        uint32_t realcolor = (color & 0xFFFF) | ((color << 16) & 0xFFFF0000);
        for(unsigned int x = 0; x < ((video_width() * video_height()) / 2); x++)
        {
            ((uint32_t *)buffer_base)[x] = realcolor;
        }
    }
    else
    {
        // TODO
    }
}

void video_fill_box(int x0, int y0, int x1, int y1, uint32_t color)
{
    if (x1 < x0)
    {
        int x2 = x1;
        x1 = x0;
        x0 = x2;
    }
    if (y1 < y0)
    {
        int y2 = y1;
        y1 = y0;
        y0 = y2;
    }

    for(int y = y0; y <= y1; y++)
    {
        for(int x = x0; x <= x1; x++)
        {
            video_draw_pixel(x, y, color);
        }
    }
}

void video_draw_pixel(int x, int y, uint32_t color)
{
    if(video_depth() == 2)
    {
        ((uint16_t *)buffer_base)[x + (y * video_width())] = color & 0xFFFF;
    }
    else
    {
        // TODO
    }
}

void video_draw_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    int dy = y1 - y0;
    int dx = x1 - x0;
    int sx, sy;

    if(dy < 0)
    {
        dy = -dy;
        sy = -1;
    }
    else
    {
        sy = 1;
    }

    if(dx < 0)
    {
        dx = -dx;
        sx = -1;
    }
    else
    {
        sx = 1;
    }

    dy <<= 1;
    dx <<= 1;

    video_draw_pixel(x0, y0, color);
    if(dx > dy)
    {
        int frac = dy - (dx >> 1);
        while(x0 != x1)
        {
            if(frac >= 0)
            {
                y0 += sy;
                frac -= dx;
            }
            x0 += sx;
            frac += dy;
            video_draw_pixel(x0, y0 , color);
        }
    }
    else
    {
        int frac = dx - (dy >> 1);
        while(y0 != y1)
        {
            if(frac >= 0)
            {
                x0 += sx;
                frac -= dy;
            }
            y0 += sy;
            frac += dx;
            video_draw_pixel(x0, y0 , color);
        }
    }
}

void video_draw_character( int x, int y, uint32_t color, char ch )
{
    for(int row = 0; row < 8; row++)
    {
        uint8_t c = __font_data[(ch * 8) + row];

        for(int col = 0; col < 8; col++)
        {
            if( c & 0x80 )
            {
                /* Only draw it if it is active */
                video_draw_pixel( x + col, y + row, color );
            }

            c <<= 1;
        }
    }
}

void video_draw_sprite( int x, int y, int width, int height, void *data )
{
    if(video_depth() == 2)
    {
        uint16_t *pixels = (uint16_t *)data;

        for(int row = 0; row < height; row++)
        {
            for(int col = 0; col < width; col++)
            {
                video_draw_pixel( x + col, y + row, pixels[col + (row * width)] );
            }
        }
    }
    else
    {
        // TODO
    }
}

void video_draw_text( int x, int y, uint32_t color, const char * const msg )
{
    if( msg == 0 ) { return; }

    int tx = x;
    int ty = y;
    const char *text = (const char *)msg;

    while( *text )
    {
        switch( *text )
        {
            case '\r':
            case '\n':
                tx = x;
                ty += 8;
                break;
            case ' ':
                tx += 8;
                break;
            case '\t':
                tx += 8 * 5;
                break;
            default:
                video_draw_character( tx, ty, color, *text );
                tx += 8;
                break;
        }

        if (tx == video_width())
        {
            tx = 0;
            ty += 8;
        }

        text++;
    }
}

void video_display()
{
    volatile unsigned int *videobase = (volatile unsigned int *)POWERVR2_BASE;
    uint32_t buffer_offset[2] = {
        0,
        video_width() * video_height() * video_depth(),
    };

    // Swap buffers in HW
    videobase[POWERVR2_FB_DISPLAY_ADDR_1] = buffer_offset[buffer_loc];
    videobase[POWERVR2_FB_DISPLAY_ADDR_2] = buffer_offset[buffer_loc] + (video_width() * video_depth());

    // Swap buffer pointer in SW
    buffer_loc = buffer_loc ? 0 : 1;
    buffer_base = (void *)((VRAM_BASE + buffer_offset[buffer_loc]) | 0xA0000000);
}
