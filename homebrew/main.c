#include <stdint.h>
#include "font.h"

#define POWERVR2_BASE 0xA05F8000
#define VRAM_BASE 0xA5000000

#define ID (0x000 >> 2)
#define REVISION (0x004 >> 2)
#define RESET (0x008 >> 2)
#define BORDER_COL (0x040 >> 2)
#define FB_DISPLAY_CFG (0x044 >> 2)
#define FB_RENDER_CFG (0x048 >> 2)
#define FB_RENDER_MODULO (0x04C >> 2)
#define FB_DISPLAY_ADDR_1 (0x050 >> 2)
#define FB_DISPLAY_ADDR_2 (0x054 >> 2)
#define FB_DISPLAY_SIZE (0x05C >> 2)
#define FB_ (0x054 >> 2)
#define FB_CLIP_X (0x068 >> 2)
#define FB_CLIP_Y (0x06C >> 2)
#define VRAM_CFG1 (0x0A0 >> 2)
#define VRAM_CFG3 (0x0A8 >> 2)
#define SYNC_LOAD (0x0D8 >> 2)
#define VBORDER (0x0DC >> 2)
#define TSP_CFG (0x0E4 >> 2)
#define HPOS (0x0EC >> 2)
#define VPOS (0x0F0 >> 2)
#define SYNC_CFG (0x0D0 >> 2)
#define SYNC_STAT (0x10C >> 2)

void wait_for_vblank()
{
    volatile unsigned int *videobase = (volatile unsigned int *)POWERVR2_BASE;

    while(!(videobase[SYNC_STAT] & 0x01ff)) { ; }
    while((videobase[SYNC_STAT] & 0x01ff)) { ; }
}

void init_video()
{
    volatile unsigned int *videobase = (volatile unsigned int *)POWERVR2_BASE;

    // Set up video timings copied from Naomi BIOS.
    videobase[VRAM_CFG3] = 0x15D1C955;
    videobase[VRAM_CFG1] = 0x00000020;

    // Reset video.
    videobase[RESET] = 0;

    // Set border color to black.
    videobase[BORDER_COL] = 0;

    // Set up frameebuffer config to enable display, set pixel mode, no line double.
    videobase[FB_DISPLAY_CFG] = (
        0x1 << 23 |  // Double pixel clock for VGA.
        0x1 << 2 |   // RGB565 mode.
        0x1 << 0     // Enable display.
    );

    // Set up framebuffer render config to dither enabled, RGB565, no alpha threshold.
    videobase[FB_RENDER_CFG] = (
        0x1 << 3 |  // Dither enabled.
        0x1 << 0    // RGB565 mode.
    );

    // Set up even/odd field video base address, shifted by bpp.
    videobase[FB_DISPLAY_ADDR_1] = 0 << 1;
    videobase[FB_DISPLAY_ADDR_2] = 640 << 1;

    // Set up render modulo, (bpp * width) / 8.
    videobase[FB_RENDER_MODULO] = (2 * 640) / 8;

    // Set up vertical position.
    videobase[VPOS] = (
        35 << 16 |  // Even position.
        35 << 0     // Odd position.
    );
    videobase[VBORDER] = (
        40 << 16 |  // Start.
        (480 + 40) << 0    // End.
    );

    // Set up horizontal position.
    videobase[HPOS] = 144;

    // Set up refresh rate.
    videobase[SYNC_LOAD] = (
        524 << 16  |  // Vsync
        857 << 0      // Hsync
    );

    // Set up display size.
    videobase[FB_DISPLAY_SIZE] = (
        1 << 20 |                   // Interlace skip modulo if we are interlaced ((width / 4) * bpp) + 1
        (480 - 1) << 10 |           // height - 1
        (((640 / 4) * 2) - 1) << 0  // ((width / 4) * bpp) - 1
    );

    // Enable display
    videobase[SYNC_CFG] = (
        1 << 8 |  // Enable video
        0 << 6 |  // VGA mode
        0 << 4 |  // Non-interlace
        0 << 2 |  // Negative H-sync
        0 << 1    // Negative V-sync
    );

    // Set up horizontal clipping to clip within 0-640.
    videobase[FB_CLIP_X] = (640 << 16) | (0 << 0);

    // Set up vertical clipping to within 0-480.
    videobase[FB_CLIP_Y] = (480 << 16) | (0 << 0);

    // Wait for vblank like games do.
    wait_for_vblank();
}

uint16_t rgb(unsigned int r, unsigned int g, unsigned int b)
{
    r = (r >> 3) & 0x1F;
    g = (g >> 3) & 0x1F;
    b = (b >> 3) & 0x1F;

    return b | (g << 6) | (r << 11);

}

void fill_screen(uint16_t color)
{
    volatile uint16_t *videobase = (volatile uint16_t *)VRAM_BASE;

    for(unsigned int x = 0; x < (640 * 480); x++)
    {
        videobase[x] = color;
    }
}

void draw_pixel(int x, int y, uint16_t color)
{
    volatile uint16_t *videobase = (volatile uint16_t *)VRAM_BASE;
    videobase[x + (y * 640)] = color;
}

void draw_line(int x0, int y0, int x1, int y1, uint16_t color)
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

    draw_pixel(x0, y0, color);
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
            draw_pixel(x0, y0 , color);
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
            draw_pixel(x0, y0 , color);
        }
    }
}

void draw_box(int x0, int y0, int x1, int y1, uint16_t color)
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
            draw_pixel(x, y, color);
        }
    }
}

void draw_character( int x, int y, uint16_t color, char ch )
{
    volatile uint16_t *buffer = (volatile uint16_t *)VRAM_BASE;

    for( int row = 0; row < 8; row++ )
    {
        unsigned char c = __font_data[(ch * 8) + row];

        for( int col = 0; col < 8; col++ )
        {
            if( c & 0x80 )
            {
                /* Only draw it if it is active */
                draw_pixel( x + col, y + row, color );
            }

            c <<= 1;
        }
    }
}

void draw_text( int x, int y, uint16_t color, const char * const msg )
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
                draw_character( tx, ty, color, *text );
                tx += 8;
                break;
        }

        text++;
    }
}

void udiv(int divisor, int dividend, int *outquotient, int *outremainder)
{
    int remainder = 0;
    int quotient = 0;

    for (int i = 31; i >= 0; i--)
    {
        // First, shift out the top bit and shift it into our collector.
        int shift_val = (dividend >> i) & 1;
        remainder = (remainder << 1) | shift_val;

        // Now, see if the remainder value is larger than our divisor,
        // if it is, the divisor goes into this digit and we output a
        // 1. Otherwise, it isn't and we output a 0.
        if (remainder < divisor)
        {
            quotient = quotient << 1;
        }
        else
        {
            // We output a 1 here, to say that the divisor goes into the
            // current remainder dividend. We also subtract the divisor
            // from the remainder dividend so we can start over collecting.
            quotient = (quotient << 1) | 1;
            remainder = remainder - divisor;
        }
    }

    *outquotient = quotient;
    *outremainder = remainder;
}

char *itoa( int value, char *str, int base)
{
    char *lut = "0123456789ABCDEF";
    char buf[32];
    int pos = 31;

    int rvalue = value;
    int neg = 0;
    if (rvalue < 0)
    {
        rvalue = -rvalue;
        neg = 1;
    }

    if (base < 2 || base > 16)
    {
        return str;
    }

    while(rvalue >= base)
    {
        int digit;
        udiv(base, rvalue, &rvalue, &digit);
        buf[pos--] = lut[digit];
    }

    buf[pos] = lut[rvalue];
    char *ptr = str;

    if (neg)
    {
        *ptr++ = '-';
    }

    while(pos < 32)
    {
        *ptr++ = buf[pos++];
    }

    *ptr = 0;

    return str;
}

void main()
{
    init_video();

    fill_screen(rgb(48, 48, 48));
    draw_line(20, 20, 100, 100, rgb(0, 255, 0));
    draw_line(100, 20, 20, 100, rgb(0, 255, 0));
    draw_line(20, 20, 100, 20, rgb(0, 255, 0));
    draw_line(20, 20, 20, 100, rgb(0, 255, 0));
    draw_line(100, 20, 100, 100, rgb(0, 255, 0));
    draw_line(20, 100, 100, 100, rgb(0, 255, 0));
    draw_text(20, 200, rgb(255, 0, 255), "This is a test...");

    unsigned int counter = 0;
    while ( 1 )
    {
        char *number = "Counter: xxxxxxxxxx";
        itoa(counter++, number + 9, 10);
        draw_box(20, 255, 20 + (8*19), 255 + 8, rgb(48, 48, 48));
        draw_text(20, 255, rgb(255, 255, 255), number);
        wait_for_vblank();
    }
}
