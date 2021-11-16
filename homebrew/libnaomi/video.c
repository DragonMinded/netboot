#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "naomi/video.h"
#include "naomi/system.h"
#include "naomi/dimmcomms.h"
#include "naomi/eeprom.h"
#include "naomi/console.h"
#include "naomi/interrupt.h"
#include "naomi/thread.h"
#include "video-internal.h"
#include "irqinternal.h"
#include "holly.h"
#include "font.h"

#define POWERVR2_BASE 0xA05F8000

#define POWERVR2_ID (0x000 >> 2)
#define POWERVR2_REVISION (0x004 >> 2)
#define POWERVR2_RESET (0x008 >> 2)
#define POWERVR2_BORDER_COL (0x040 >> 2)
#define POWERVR2_FB_DISPLAY_CFG (0x044 >> 2)
#define POWERVR2_FB_RENDER_CFG (0x048 >> 2)
#define POWERVR2_FB_RENDER_MODULO (0x04C >> 2)
#define POWERVR2_FB_DISPLAY_ADDR_1 (0x050 >> 2)
#define POWERVR2_FB_DISPLAY_ADDR_2 (0x054 >> 2)
#define POWERVR2_FB_DISPLAY_SIZE (0x05C >> 2)
#define POWERVR2_FB_CLIP_X (0x068 >> 2)
#define POWERVR2_FB_CLIP_Y (0x06C >> 2)
#define POWERVR2_VRAM_CFG1 (0x0A0 >> 2)
#define POWERVR2_VRAM_CFG2 (0x0A4 >> 2)
#define POWERVR2_VRAM_CFG3 (0x0A8 >> 2)
#define POWERVR2_VBLANK_INTERRUPT (0x0CC >> 2)
#define POWERVR2_SYNC_CFG (0x0D0 >> 2)
#define POWERVR2_HBLANK (0x0D4 >> 4)
#define POWERVR2_SYNC_LOAD (0x0D8 >> 2)
#define POWERVR2_VBORDER (0x0DC >> 2)
#define POWERVR2_TSP_CFG (0x0E4 >> 2)
#define POWERVR2_VIDEO_CFG (0x0E8 >> 2)
#define POWERVR2_HPOS (0x0EC >> 2)
#define POWERVR2_VPOS (0x0F0 >> 2)
#define POWERVR2_SYNC_STAT (0x10C >> 2)

#define DISPLAY_CFG_RGB1555 0
#define DISPLAY_CFG_RGB565 1
#define DISPLAY_CFG_RGB888 2
#define DISPLAY_CFG_RGB0888 3

#define RENDER_CFG_RGB0555 0
#define RENDER_CFG_RGB565 1
#define RENDER_CFG_ARGB4444 2
#define RENDER_CFG_ARGB1555 3
#define RENDER_CFG_RGB888 4
#define RENDER_CFG_RGB0888 5
#define RENDER_CFG_ARGB8888 6
// Mode 7 appears to be a redefinition of mode 2.

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

// TODO: Need to support more than 640x480 framebuffer mode.
// TODO: Need to support more than RGB1555 color.


// Static members that don't need to be accessed anywhere else.
static uint32_t global_background_color = 0;
static uint32_t global_background_fill_start = 0;
static uint32_t global_background_fill_end = 0;
static uint32_t global_background_fill_color[8] = { 0 };
static unsigned int global_background_set = 0;

// We only use two of these for rendering. The third is so we can
// give a pointer out to scratch VRAM for other code to use.
static uint32_t global_buffer_offset[3] = { 0, 0, 0 };

// Remember HBLANK/VBLANK set up by BIOS in case we need to return there.
static uint32_t saved_hvint = 0;
static uint32_t initialized = 0;

// Nonstatic so that other video modules can use it as well.
unsigned int global_video_width = 0;
unsigned int global_video_height = 0;
unsigned int cached_actual_width = 0;
unsigned int cached_actual_height = 0;
unsigned int global_video_depth = 0;
unsigned int global_video_vertical = 0;
void *buffer_base = 0;

// Our current framebuffer location, for double buffering.
unsigned int buffer_loc = 0;
#define current_buffer_loc ((buffer_loc) ? 1 : 0)
#define next_buffer_loc ((buffer_loc) ? 0 : 1)

void video_display_on_vblank()
{
    volatile unsigned int *videobase = (volatile unsigned int *)POWERVR2_BASE;

    // Draw any registered console to the screen.
    console_render();

    // Handle filling the background of the other screen while we wait.
    if (global_background_set)
    {
        global_background_fill_start = ((VRAM_BASE + global_buffer_offset[next_buffer_loc]) | 0xA0000000);
        global_background_fill_end = global_background_fill_start + ((global_video_width * global_video_height * global_video_depth));
    }
    else
    {
        global_background_fill_start = 0;
        global_background_fill_end = 0;
    }

    // First, figure out if we're running with disabled interrupts. If so, we can't use
    // the hardware to wait for VBLANK.
    uint32_t old_interrupts = irq_disable();
    unsigned int irqs_disabled = _irq_was_disabled(old_interrupts);
    irq_restore(old_interrupts);

    if (irqs_disabled)
    {
        // Wait for us to enter the VBLANK portion of the frame scan. This is the same
        // spot that we would get a VBLANK interrupt if we were using threads.
        uint32_t vblank_in_position = videobase[POWERVR2_VBLANK_INTERRUPT] & 0x1FF;
        while((videobase[POWERVR2_SYNC_STAT] & 0x1FF) != vblank_in_position) { ; }

        // Swap buffers in HW.
        videobase[POWERVR2_FB_DISPLAY_ADDR_1] = global_buffer_offset[current_buffer_loc];
        videobase[POWERVR2_FB_DISPLAY_ADDR_2] = global_buffer_offset[current_buffer_loc] + (global_video_width * global_video_depth);

        // Swap buffer pointer in SW.
        buffer_loc = next_buffer_loc;
        buffer_base = (void *)((VRAM_BASE + global_buffer_offset[current_buffer_loc]) | 0xA0000000);
    }
    else
    {
        // Wait for hardware vblank interrupt.
        thread_wait_vblank_in();

        // Swap buffers in HW.
        videobase[POWERVR2_FB_DISPLAY_ADDR_1] = global_buffer_offset[current_buffer_loc];
        videobase[POWERVR2_FB_DISPLAY_ADDR_2] = global_buffer_offset[current_buffer_loc] + (global_video_width * global_video_depth);

        // Swap buffer pointer in SW.
        buffer_loc = next_buffer_loc;
        buffer_base = (void *)((VRAM_BASE + global_buffer_offset[current_buffer_loc]) | 0xA0000000);

        // No longer need our high priority status, yield to other threads.
        thread_yield();
    }

    // Finish filling in the background. Gotta do this now, fast or slow, because
    // when we exit this function the user is fully expected to start drawing new
    // graphics.
    if (global_background_fill_start < global_background_fill_end)
    {
        // Try the fast way, and if we don't have the HW access (another thread owned it before we disabled irqs),
        // then we need to do it the slow way.
        if (hw_memset((void *)global_background_fill_start, global_background_fill_color[0], global_background_fill_end - global_background_fill_start) == 0)
        {
            while (global_background_fill_start < global_background_fill_end)
            {
                memcpy((void *)global_background_fill_start, global_background_fill_color, 32);
                global_background_fill_start += 32;
            }
        }
    }

    // Set these back to empty, since we no longer need to handle them.
    global_background_fill_start = 0;
    global_background_fill_end = 0;
}

unsigned int video_width()
{
    return cached_actual_width;
}

unsigned int video_height()
{
    return cached_actual_height;
}

unsigned int video_depth()
{
    return global_video_depth;
}

void *video_framebuffer()
{
    return buffer_base;
}

unsigned int video_is_vertical()
{
    return global_video_vertical;
}

void _vblank_init()
{
    uint32_t old_interrupts = irq_disable();
    if ((HOLLY_INTERNAL_IRQ_2_MASK & HOLLY_INTERNAL_INTERRUPT_VBLANK_IN) == 0)
    {
        HOLLY_INTERNAL_IRQ_2_MASK = HOLLY_INTERNAL_IRQ_2_MASK | HOLLY_INTERNAL_INTERRUPT_VBLANK_IN;
    }
    if ((HOLLY_INTERNAL_IRQ_2_MASK & HOLLY_INTERNAL_INTERRUPT_VBLANK_OUT) == 0)
    {
        HOLLY_INTERNAL_IRQ_2_MASK = HOLLY_INTERNAL_IRQ_2_MASK | HOLLY_INTERNAL_INTERRUPT_VBLANK_OUT;
    }
    if ((HOLLY_INTERNAL_IRQ_2_MASK & HOLLY_INTERNAL_INTERRUPT_HBLANK) == 0)
    {
        HOLLY_INTERNAL_IRQ_2_MASK = HOLLY_INTERNAL_IRQ_2_MASK | HOLLY_INTERNAL_INTERRUPT_HBLANK;
    }
    irq_restore(old_interrupts);
}

void _vblank_free()
{
    uint32_t old_interrupts = irq_disable();
    if ((HOLLY_INTERNAL_IRQ_2_MASK & HOLLY_INTERNAL_INTERRUPT_VBLANK_IN) != 0)
    {
        HOLLY_INTERNAL_IRQ_2_MASK = HOLLY_INTERNAL_IRQ_2_MASK & (~HOLLY_INTERNAL_INTERRUPT_VBLANK_IN);
    }
    if ((HOLLY_INTERNAL_IRQ_2_MASK & HOLLY_INTERNAL_INTERRUPT_VBLANK_OUT) != 0)
    {
        HOLLY_INTERNAL_IRQ_2_MASK = HOLLY_INTERNAL_IRQ_2_MASK & (~HOLLY_INTERNAL_INTERRUPT_VBLANK_OUT);
    }
    if ((HOLLY_INTERNAL_IRQ_2_MASK & HOLLY_INTERNAL_INTERRUPT_HBLANK) != 0)
    {
        HOLLY_INTERNAL_IRQ_2_MASK = HOLLY_INTERNAL_IRQ_2_MASK & (~HOLLY_INTERNAL_INTERRUPT_HBLANK);
    }
    irq_restore(old_interrupts);
}

// TODO: This function assumes 640x480 VGA, we should support more varied options.
void video_init_simple()
{
    uint32_t old_interrupts = irq_disable();
    volatile unsigned int *videobase = (volatile unsigned int *)POWERVR2_BASE;

    global_video_width = 640;
    global_video_height = 480;
    global_video_depth = 2;
    global_background_color = 0;
    global_background_set = 0;
    global_buffer_offset[0] = 0;
    global_buffer_offset[1] = global_buffer_offset[0] + (global_video_width * global_video_height * global_video_depth);
    global_buffer_offset[2] = global_buffer_offset[1] + (global_video_width * global_video_height * global_video_depth);

    // First, read the EEPROM and figure out if we're vertical orientation.
    eeprom_t eeprom;
    eeprom_read(&eeprom);
    global_video_vertical = eeprom.system.monitor_orientation == MONITOR_ORIENTATION_VERTICAL ? 1 : 0;

    if (global_video_vertical) {
        cached_actual_width = global_video_height;
    } else {
        cached_actual_width = global_video_width;
    }
    if (global_video_vertical) {
        cached_actual_height = global_video_width;
    } else {
        cached_actual_height = global_video_height;
    }

    // Now, initialize the tile accelerator so it can be used for drawing.
    _ta_init();

    // Now, zero out the screen so there's no garbage if we never display.
    void *zero_base = (void *)(VRAM_BASE | 0xA0000000);
    if (!hw_memset(zero_base, 0, global_video_width * global_video_height * global_video_depth * 2))
    {
        // Gotta do the slow method.
        memset(zero_base, 0, global_video_width * global_video_height * global_video_depth * 2);
    }

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
        0x1 << 23 |                 // Double pixel clock for VGA.
        DISPLAY_CFG_RGB1555 << 2 |  // RGB1555 mode.
        0x1 << 0                    // Enable display.
    );

    // Set up framebuffer render config to dither enabled, RGB0555, no alpha threshold.
    videobase[POWERVR2_FB_RENDER_CFG] = (
        0x1 << 3 |               // Dither enabled.
        RENDER_CFG_RGB0555 << 0  // RGB555 mode, no alpha threshold.
    );

    // Set up even/odd field video base address, shifted by bpp.
    videobase[POWERVR2_FB_DISPLAY_ADDR_1] = global_buffer_offset[current_buffer_loc];
    videobase[POWERVR2_FB_DISPLAY_ADDR_2] = global_buffer_offset[current_buffer_loc] + (global_video_width * global_video_depth);

    // Swap buffer pointer in SW.
    buffer_loc = next_buffer_loc;
    buffer_base = (void *)((VRAM_BASE + global_buffer_offset[current_buffer_loc]) | 0xA0000000);

    // Set up render modulo, (bpp * width) / 8.
    videobase[POWERVR2_FB_RENDER_MODULO] = (global_video_depth * global_video_width) / 8;

    // Set up vertical position.
    videobase[POWERVR2_VPOS] = (
        35 << 16 |  // Even position.
        35 << 0     // Odd position.
    );
    videobase[POWERVR2_VBORDER] = (
        40 << 16 |                       // Start.
        (global_video_height + 40) << 0  // End.
    );
    if (!initialized)
    {
        saved_hvint = videobase[POWERVR2_VBLANK_INTERRUPT];
        initialized = 1;
    }
    videobase[POWERVR2_VBLANK_INTERRUPT] = (
        40 << 16 |                       // Out of vblank.
        (global_video_height + 40) << 0  // In vblank.
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
        1 << 20 |                                                   // Interlace skip modulo if we are interlaced ((width / 4) * bpp) + 1
        (global_video_height - 1) << 10 |                           // height - 1
        (((global_video_width / 4) * global_video_depth) - 1) << 0  // ((width / 4) * bpp) - 1
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
    videobase[POWERVR2_FB_CLIP_X] = (global_video_width << 16) | (0 << 0);

    // Set up vertical clipping to within 0-480.
    videobase[POWERVR2_FB_CLIP_Y] = (global_video_height << 16) | (0 << 0);

    // Wait for vblank like games do.
    uint32_t vblank_in_position = videobase[POWERVR2_VBLANK_INTERRUPT] & 0x1FF;
    while((videobase[POWERVR2_SYNC_STAT] & 0x1FF) != vblank_in_position) { ; }

    // Now, ask the TA to set up its buffers since we have working video now.
    _ta_init_buffers();

    // Finally, its safe to enable interrupts and move on.
    irq_restore(old_interrupts);
}

void video_free()
{
    uint32_t old_interrupts = irq_disable();
    volatile unsigned int *videobase = (volatile unsigned int *)POWERVR2_BASE;

    // Disable display
    videobase[POWERVR2_SYNC_CFG] = (
        0 << 8 |  // Disable video
        0 << 6 |  // VGA mode
        0 << 4 |  // Non-interlace
        0 << 2 |  // Negative H-sync
        0 << 1    // Negative V-sync
    );

    if (initialized)
    {
        initialized = 0;
        videobase[POWERVR2_VBLANK_INTERRUPT] = saved_hvint;
    }

    // Kill our vblank interrupt.
    _vblank_free();

    // Kill the tile accelerator.
    _ta_free();

    // De-init our globals.
    global_video_width = 0;
    global_video_height = 0;
    global_video_depth = 0;
    global_background_color = 0;
    global_background_set = 0;
    global_buffer_offset[0] = 0;
    global_buffer_offset[1] = 0;
    global_buffer_offset[2] = 0;

    // We're done, safe for interrupts to come back.
    irq_restore(old_interrupts);
}

uint32_t rgb(unsigned int r, unsigned int g, unsigned int b)
{
    if(global_video_depth == 2)
    {
        // Make a 1555 color that is non-transparent.
        return RGB0555(r, g, b);
    }
    else if(global_video_depth == 4)
    {
        // TODO: 32-bit video modes.
        return 0;
    }
    else
    {
        return 0;
    }
}

uint32_t rgba(unsigned int r, unsigned int g, unsigned int b, unsigned int a)
{
    if(global_video_depth == 2)
    {
        // Make a 1555 color that is transparent if a < 128 and opaque if a >= 128.
        return RGB1555(r, g, b, a);
    }
    else if(global_video_depth == 4)
    {
        // TODO: 32-bit video modes.
        return 0;
    }
    else
    {
        return 0;
    }
}

void explodergb(uint32_t color, unsigned int *r, unsigned int *g, unsigned int *b)
{
    if(global_video_depth == 2)
    {
        EXPLODE0555(color, *r, *g, *b);
    }
    else if(global_video_depth == 4)
    {
        // TODO: 32-bit video modes.
        *r = 0;
        *g = 0;
        *b = 0;
    }
}

void explodergba(uint32_t color, unsigned int *r, unsigned int *g, unsigned int *b, unsigned int *a)
{
    if(global_video_depth == 2)
    {
        EXPLODE1555(color, *r, *g, *b, *a);
    }
    else if(global_video_depth == 4)
    {
        // TODO: 32-bit video modes.
        *r = 0;
        *g = 0;
        *b = 0;
        *a = 0;
    }
}

void video_fill_screen(uint32_t color)
{
    if(global_video_depth == 2)
    {
        if (!hw_memset(buffer_base, (color & 0xFFFF) | ((color << 16) & 0xFFFF0000), global_video_width * global_video_height * 2))
        {
            memset(buffer_base, (color & 0xFFFF) | ((color << 16) & 0xFFFF0000), global_video_width * global_video_height * 2);
        }
    }
    else if(global_video_depth == 4)
    {
        if (!hw_memset(buffer_base, color, global_video_width * global_video_height * 4))
        {
            memset(buffer_base, color, global_video_width * global_video_height * 4);
        }
    }
}

void video_set_background_color(uint32_t color)
{
    video_fill_screen(color);
    global_background_set = 1;

    if(global_video_depth == 2)
    {
        for (int offset = 0; offset < 8; offset++)
        {
            global_background_fill_color[offset] = (color & 0xFFFF) | ((color << 16) & 0xFFFF0000);
        }
    }
    else if(global_video_depth == 4)
    {
        for (int offset = 0; offset < 8; offset++)
        {
            global_background_fill_color[offset] = color;
        }
    }
}

void video_fill_box(int x0, int y0, int x1, int y1, uint32_t color)
{
    int low_x;
    int high_x;
    int low_y;
    int high_y;

    if (x1 < x0)
    {
        low_x = x1;
        high_x = x0;
    }
    else
    {
        low_x = x0;
        high_x = x1;
    }
    if (y1 < y0)
    {
        low_y = y1;
        high_y = y0;
    }
    else
    {
        low_y = y0;
        high_y = y1;
    }

    if (high_x < 0 || low_x >= cached_actual_width || high_y < 0 || low_y >= cached_actual_height)
    {
        return;
    }
    if (low_x < 0)
    {
        low_x = 0;
    }
    if (low_y < 0)
    {
        low_y = 0;
    }
    if (high_x >= cached_actual_width)
    {
        high_x = cached_actual_width - 1;
    }
    if (high_y >= cached_actual_height)
    {
        high_y = cached_actual_height - 1;
    }

    if(global_video_depth == 2)
    {
        if(global_video_vertical)
        {
            for(int col = low_x; col <= high_x; col++)
            {
                for(int row = high_y; row >= low_y; row--)
                {
                    SET_PIXEL_V_2(buffer_base, col, row, color);
                }
            }
        }
        else
        {
            for(int row = low_y; row <= high_y; row++)
            {
                for(int col = low_x; col <= high_x; col++)
                {
                    SET_PIXEL_H_2(buffer_base, col, row, color);
                }
            }
        }
    }
    else if(global_video_depth == 4)
    {
        // TODO: 32-bit video modes.
    }
}

void video_draw_pixel(int x, int y, uint32_t color)
{
    if (global_video_depth == 2)
    {
        if (global_video_vertical)
        {
            SET_PIXEL_V_2(buffer_base, x, y, color);
        }
        else
        {
            SET_PIXEL_H_2(buffer_base, x, y, color);
        }
    }
    else if(global_video_depth == 4)
    {
        if (global_video_vertical)
        {
            SET_PIXEL_V_4(buffer_base, x, y, color);
        }
        else
        {
            SET_PIXEL_H_4(buffer_base, x, y, color);
        }
    }
}

uint32_t video_get_pixel(int x, int y)
{
    if (global_video_depth == 2)
    {
        if (global_video_vertical)
        {
            return GET_PIXEL_V_2(buffer_base, x, y);
        }
        else
        {
            return GET_PIXEL_H_2(buffer_base, x, y);
        }
    }
    else if(global_video_depth == 4)
    {
        if (global_video_vertical)
        {
            return GET_PIXEL_V_4(buffer_base, x, y);
        }
        else
        {
            return GET_PIXEL_H_4(buffer_base, x, y);
        }
    }
    else
    {
        return 0;
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
            video_draw_pixel(x0, y0, color);
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
            video_draw_pixel(x0, y0, color);
        }
    }
}

void video_draw_box(int x0, int y0, int x1, int y1, uint32_t color)
{
    int low_x;
    int high_x;
    int low_y;
    int high_y;

    if (x1 < x0)
    {
        low_x = x1;
        high_x = x0;
    }
    else
    {
        low_x = x0;
        high_x = x1;
    }
    if (y1 < y0)
    {
        low_y = y1;
        high_y = y0;
    }
    else
    {
        low_y = y0;
        high_y = y1;
    }

    video_draw_line(low_x, low_y, high_x, low_y, color);
    video_draw_line(low_x, high_y, high_x, high_y, color);
    video_draw_line(low_x, low_y, low_x, high_y, color);
    video_draw_line(high_x, low_y, high_x, high_y, color);
}

void video_draw_debug_character( int x, int y, uint32_t color, char ch )
{
    if (ch < 0x20 || ch > 0x7F)
    {
        return;
    }

    for(int row = y; row < y + 8; row++)
    {
        uint8_t c = __font_data[(ch * 8) + (row - y)];

        /* Draw top half unrolled */
        switch( c & 0xF0 )
        {
            case 0x10:
                video_draw_pixel( x + 3, row, color );
                break;
            case 0x20:
                video_draw_pixel( x + 2, row, color );
                break;
            case 0x30:
                video_draw_pixel( x + 2, row, color );
                video_draw_pixel( x + 3, row, color );
                break;
            case 0x40:
                video_draw_pixel( x + 1, row, color );
                break;
            case 0x50:
                video_draw_pixel( x + 1, row, color );
                video_draw_pixel( x + 3, row, color );
                break;
            case 0x60:
                video_draw_pixel( x + 1, row, color );
                video_draw_pixel( x + 2, row, color );
                break;
            case 0x70:
                video_draw_pixel( x + 1, row, color );
                video_draw_pixel( x + 2, row, color );
                video_draw_pixel( x + 3, row, color );
                break;
            case 0x80:
                video_draw_pixel( x, row, color );
                break;
            case 0x90:
                video_draw_pixel( x, row, color );
                video_draw_pixel( x + 3, row, color );
                break;
            case 0xA0:
                video_draw_pixel( x, row, color );
                video_draw_pixel( x + 2, row, color );
                break;
            case 0xB0:
                video_draw_pixel( x, row, color );
                video_draw_pixel( x + 2, row, color );
                video_draw_pixel( x + 3, row, color );
                break;
            case 0xC0:
                video_draw_pixel( x, row, color );
                video_draw_pixel( x + 1, row, color );
                break;
            case 0xD0:
                video_draw_pixel( x, row, color );
                video_draw_pixel( x + 1, row, color );
                video_draw_pixel( x + 3, row, color );
                break;
            case 0xE0:
                video_draw_pixel( x, row, color );
                video_draw_pixel( x + 1, row, color );
                video_draw_pixel( x + 2, row, color );
                break;
            case 0xF0:
                video_draw_pixel( x, row, color );
                video_draw_pixel( x + 1, row, color );
                video_draw_pixel( x + 2, row, color );
                video_draw_pixel( x + 3, row, color );
                break;
        }

        /* Draw bottom half unrolled */
        switch( c & 0x0F )
        {
            case 0x01:
                video_draw_pixel( x + 7, row, color );
                break;
            case 0x02:
                video_draw_pixel( x + 6, row, color );
                break;
            case 0x03:
                video_draw_pixel( x + 6, row, color );
                video_draw_pixel( x + 7, row, color );
                break;
            case 0x04:
                video_draw_pixel( x + 5, row, color );
                break;
            case 0x05:
                video_draw_pixel( x + 5, row, color );
                video_draw_pixel( x + 7, row, color );
                break;
            case 0x06:
                video_draw_pixel( x + 5, row, color );
                video_draw_pixel( x + 6, row, color );
                break;
            case 0x07:
                video_draw_pixel( x + 5, row, color );
                video_draw_pixel( x + 6, row, color );
                video_draw_pixel( x + 7, row, color );
                break;
            case 0x08:
                video_draw_pixel( x + 4, row, color );
                break;
            case 0x09:
                video_draw_pixel( x + 4, row, color );
                video_draw_pixel( x + 7, row, color );
                break;
            case 0x0A:
                video_draw_pixel( x + 4, row, color );
                video_draw_pixel( x + 6, row, color );
                break;
            case 0x0B:
                video_draw_pixel( x + 4, row, color );
                video_draw_pixel( x + 6, row, color );
                video_draw_pixel( x + 7, row, color );
                break;
            case 0x0C:
                video_draw_pixel( x + 4, row, color );
                video_draw_pixel( x + 5, row, color );
                break;
            case 0x0D:
                video_draw_pixel( x + 4, row, color );
                video_draw_pixel( x + 5, row, color );
                video_draw_pixel( x + 7, row, color );
                break;
            case 0x0E:
                video_draw_pixel( x + 4, row, color );
                video_draw_pixel( x + 5, row, color );
                video_draw_pixel( x + 6, row, color );
                break;
            case 0x0F:
                video_draw_pixel( x + 4, row, color );
                video_draw_pixel( x + 5, row, color );
                video_draw_pixel( x + 6, row, color );
                video_draw_pixel( x + 7, row, color );
                break;
        }
    }
}

void video_draw_sprite( int x, int y, int width, int height, void *data )
{
    int low_x = 0;
    int high_x = width;
    int low_y = 0;
    int high_y = height;

    if (x < 0)
    {
        if (x + width <= 0)
        {
            return;
        }

        low_x = -x;
    }
    if (y < 0)
    {
        if (y + height <= 0)
        {
            return;
        }

        low_y = -y;
    }
    if ((x + width) >= cached_actual_width)
    {
        if (x >= cached_actual_width)
        {
            return;
        }

        high_x = cached_actual_width - x;
    }
    if (y + height >= cached_actual_height)
    {
        if (y >= cached_actual_height)
        {
            return;
        }

        high_y = cached_actual_height - y;
    }

    if(global_video_depth == 2)
    {
        uint16_t *pixels = (uint16_t *)data;

        if(global_video_vertical)
        {
            for(int col = low_x; col < high_x; col++)
            {
                for(int row = (high_y - 1); row >= low_y; row--)
                {
                    uint16_t pixel = pixels[col + (row * width)];
                    if (pixel & 0x8000)
                    {
                        SET_PIXEL_V_2(buffer_base, x + col, y + row, pixel);
                    }
                }
            }
        }
        else
        {
            for(int row = low_y; row < high_y; row++)
            {
                for(int col = low_x; col < high_x; col++)
                {
                    uint16_t pixel = pixels[col + (row * width)];
                    if (pixel & 0x8000)
                    {
                        SET_PIXEL_H_2(buffer_base, x + col, y + row, pixel);
                    }
                }
            }
        }
    }
    else if(global_video_depth == 4)
    {
        // TODO: 32-bit video modes.
    }
}

void __video_draw_debug_text( int x, int y, uint32_t color, const char * const msg )
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
                video_draw_debug_character( tx, ty, color, *text );
                tx += 8;
                break;
        }

        if ((tx + 8) >= cached_actual_width)
        {
            tx = 0;
            ty += 8;
        }

        text++;
    }
}

void video_draw_debug_text(int x, int y, uint32_t color, const char * const msg, ...)
{
    if (msg)
    {
        char buffer[2048];
        va_list args;
        va_start(args, msg);
        int length = vsnprintf(buffer, 2047, msg, args);
        va_end(args);

        if (length > 0)
        {
            buffer[min(length, 2047)] = 0;
            __video_draw_debug_text(x, y, color, buffer);
        }
    }
}

void *video_scratch_area()
{
    return(void *)((VRAM_BASE + global_buffer_offset[2]) | 0xA0000000);
}
