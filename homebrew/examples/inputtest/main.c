#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "font.h"

#define POWERVR2_BASE 0xA05F8000

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

#define VRAM_BASE 0xA5000000

#define MAPLE_BASE 0xA05F6C00

#define DMA_BUFFER_ADDR (0x04 >> 2)
#define DMA_START_HW (0x10 >> 2)
#define MAPLE_DEVICE_ENABLE (0x14 >> 2)
#define DMA_START (0x18 >> 2)
#define TIMEOUT_AND_SPEED (0x80 >> 2)
#define HW_INIT (0x8C >> 2)

#define DEVICE_INFO_REQUEST 0x01
#define DEVICE_RESET_REQUEST 0x03
#define COMMAND_ACKNOWLEDGE_RESPONSE 0x07

#define UNCACHED_MIRROR 0xA0000000
#define PHYSICAL_MASK 0x0FFFFFFF

uint16_t *buffer_base = 0;
char *console_base = 0;
uint8_t *maple_base = 0;

#define console_printf(...) sprintf(console_base + strlen(console_base), __VA_ARGS__)

void video_wait_for_vblank()
{
    volatile unsigned int *videobase = (volatile unsigned int *)POWERVR2_BASE;

    while(!(videobase[SYNC_STAT] & 0x01ff)) { ; }
    while((videobase[SYNC_STAT] & 0x01ff)) { ; }
}

void video_init()
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
    videobase[HPOS] = 166;

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
    video_wait_for_vblank();

    // Set up a double buffer rendering location.
    console_base = malloc(((640*480) / (8*8)) + 1);
    buffer_base = malloc(640 * 480 * 2);
    console_base[0] = 0;
}

void maple_wait_for_dma()
{
    volatile unsigned int *maplebase = (volatile unsigned int *)MAPLE_BASE;

    // Wait until the DMA_START bit has gone back to 0.
    while((maplebase[DMA_START] & 1) != 0) { ; }
}

void maple_init()
{
    volatile unsigned int *maplebase = (volatile unsigned int *)MAPLE_BASE;

    // Maple init routines based on Mvc2.
    maplebase[HW_INIT] = 0x6155404F;
    maplebase[DMA_START_HW] = 0;

    // Set up timeout and bitrate.
    maplebase[TIMEOUT_AND_SPEED] = (50000 << 16) | 0;

    // Enable maple bus.
    maplebase[MAPLE_DEVICE_ENABLE] = 1;

    // Wait for any DMA transfer to finish, like real HW does.
    maple_wait_for_dma();

    // Allocate enough memory for a request and a response, as well as
    // 32 bytes of padding.
    maple_base = malloc(1024 + 1024 + 32);
}

void maple_print_regs()
{
    volatile unsigned int *maplebase = (volatile unsigned int *)MAPLE_BASE;

    console_printf("DMA_BUFFER_ADDR: %08X\n", maplebase[DMA_BUFFER_ADDR]);
    console_printf("DMA_START_HW: %08X\n", maplebase[DMA_START_HW]);
    console_printf("MAPLE_DEVICE_ENABLE: %08X\n", maplebase[MAPLE_DEVICE_ENABLE]);
    console_printf("DMA_START: %08X\n", maplebase[DMA_START]);
    console_printf("TIMEOUT_AND_SPEED: %08X\n", maplebase[TIMEOUT_AND_SPEED]);
    console_printf("HW_INIT: %08X\n", maplebase[HW_INIT]);
}

volatile uint32_t *maple_swap_data(unsigned int port, int peripheral, unsigned int cmd, unsigned int datalen, uint32_t *data)
{
    volatile uint32_t *maplebase = (volatile uint32_t *)MAPLE_BASE;

    // First, calculate the send and receive buffers. We make sure we get a 32-byte
    // aligned address, and ensure its in uncached memory.
    volatile uint32_t *recv = (uint32_t *)(((((uint32_t)maple_base) + 31) & ~31) | UNCACHED_MIRROR);
    // Place the send buffer 1024 bytes after the receive buffer.
    volatile uint32_t *send = recv + (1024 / sizeof(recv[0]));

    // Calculate the recipient address.
    unsigned int addr;
    if (peripheral == 0)
    {
        // Main controller peripheral.
        addr = (port & 0x3) << 6 | 0x20;
    }
    else
    {
        // Sub peripheral.
        addr = (port & 0x3) << 6 | (1 << (peripheral - 1)) & 0x1F;
    }

    // Calculate receive buffer
    uint32_t buffer = (uint32_t)recv & PHYSICAL_MASK;

    // Wait until any transfer finishes before messing with memory, then point at
    // our buffer.
    maple_wait_for_dma();

    // Now, construct the maple request transfer descriptor.
    memset((void *)send, 0, 1024);
    send[0] = (
        1 << 31 |       // This is the last entry in the transfer descriptor.
        datalen & 0xFF  // Length is how many extra bytes of payload we are including.
    );
    send[1] = buffer;
    send[2] = (
        /*
        ((cmd & 0xFF) << 24) |  // The command we are sending.
        (addr << 16) |          // The recipient of our packet.
        (0 << 8) |              // The sender address (us).
        (datalen & 0xFF)        // Number of words we tack on the end.
        */
        ((cmd & 0xFF)) |         // The command we are sending.
        ((addr & 0xFF) << 8) |   // The recipient of our packet.
        ((addr & 0xC0) << 16) |  // The sender address (us).
        ((datalen & 0xFF) < 24)  // Number of words we tack on the end.
    );

    // Add on any command data we should include.
    if (datalen)
    {
        memcpy((void *)&send[3], data, datalen * 4);
    }

    // Set the first word of the recv buffer like real BIOS does.
    memset((void *)recv, 0, 1024);
    recv[0] = 0xFFFFFFFF;

    // Debugging
    console_printf("Send buffer: %08X\n", (uint32_t)send);
    console_printf("Recv buffer: %08X\n", (uint32_t)recv);
    console_printf("Sending:\n   ");
    for (int i = 0; i < (3 + datalen); i++)
    {
        volatile uint8_t *bytes = (volatile uint8_t *)(&send[i]);
        console_printf(" %02X %02X %02X %02X", bytes[0], bytes[1], bytes[2], bytes[3]);
    }
    console_printf("\n");

    // Kick off the DMA request
    maple_wait_for_dma();
    maplebase[DMA_BUFFER_ADDR] = (uint32_t)send & PHYSICAL_MASK;
    maplebase[MAPLE_DEVICE_ENABLE] = 1;
    maplebase[DMA_START] = 1;

    // Wait for it to finish
    maple_wait_for_dma();

    // Return the receive buffer.
    return recv;
}

uint16_t rgbto565(unsigned int r, unsigned int g, unsigned int b)
{
    r = (r >> 3) & 0x1F;
    g = (g >> 2) & 0x3F;
    b = (b >> 3) & 0x1F;

    return b | (g << 5) | (r << 11);

}

void fill_screen(uint16_t color)
{
    for(unsigned int x = 0; x < (640 * 480); x++)
    {
        buffer_base[x] = color;
    }
}

void draw_pixel(int x, int y, uint16_t color)
{
    buffer_base[x + (y * 640)] = color;
}

void draw_character( int x, int y, uint16_t color, char ch )
{
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

void display()
{
    // Render a simple test console.
    fill_screen(rgbto565(48, 48, 48));
    draw_text(0, 0, rgbto565(255, 255, 255), console_base);
    video_wait_for_vblank();

    // Copy it to VRAM.
    memcpy((void *)VRAM_BASE, buffer_base, 640 * 480 * 2);
}

void main()
{
    volatile uint32_t *resp;

    // Set up a crude console
    video_init();
    maple_init();

    unsigned int try = 0;
    do
    {
        if(try > 0)
        {
            // Spinloop, reset our console.
            console_base[0] = 0;
            for(int x = 0x2710; x > 0; x--) { ; }
        }

        // Try again...
        resp = maple_swap_data(0, 0, DEVICE_INFO_REQUEST, 0, NULL);
        console_printf("Requesting Maple status try %d...\n", try++);
        console_printf("Maple returned (%08X): %08X %08X %08X %08X\n", (uint32_t)resp, resp[0], resp[1], resp[2], resp[3]);

        resp = (uint32_t *)(((uint32_t)resp) & 0x0FFFFFFF);
        console_printf("Maple returned (%08X): %08X %08X %08X %08X\n", (uint32_t)resp, resp[0], resp[1], resp[2], resp[3]);
        resp = (uint32_t *)((((uint32_t)resp) & 0x0FFFFFFF) | 0x80000000);
        console_printf("Maple returned (%08X): %08X %08X %08X %08X\n", (uint32_t)resp, resp[0], resp[1], resp[2], resp[3]);
        resp = (uint32_t *)((((uint32_t)resp) & 0x0FFFFFFF) | 0xC0000000);
        console_printf("Maple returned (%08X): %08X %08X %08X %08X\n", (uint32_t)resp, resp[0], resp[1], resp[2], resp[3]);

        display();
    }
    while (resp[0] == 0xFFFFFFFF);

    while ( 1 )
    {
        display();

    }
}

void test()
{
    video_init();

    fill_screen(rgbto565(48, 48, 48));
    draw_text(320 - 56, 236, rgbto565(255, 255, 255), "test mode stub");
    video_wait_for_vblank();

    // Copy it to VRAM.
    memcpy((void *)VRAM_BASE, buffer_base, 640 * 480 * 2);

    while ( 1 ) { ; }
}
