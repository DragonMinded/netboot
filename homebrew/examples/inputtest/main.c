#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "naomi/video.h"

#define MAPLE_BASE 0xA05F6C00

#define MAPLE_DMA_BUFFER_ADDR (0x04 >> 2)
#define MAPLE_DMA_TRIGGER_SELECT (0x10 >> 2)
#define MAPLE_DEVICE_ENABLE (0x14 >> 2)
#define MAPLE_DMA_START (0x18 >> 2)
#define MAPLE_TIMEOUT_AND_SPEED (0x80 >> 2)
#define MAPLE_STATUS (0x84 >> 2)
#define MAPLE_DMA_TRIGGER_CLEAR (0x88 >> 2)
#define MAPLE_DMA_HW_INIT (0x8C >> 2)
#define MAPLE_ENDIAN_SELECT (0x0E8 >> 2)

#define MAPLE_ADDRESS_RANGE(x) ((x >> 20) - 0x80)

#define DEVICE_INFO_REQUEST 0x01
#define DEVICE_RESET_REQUEST 0x03
#define DEVICE_INFO_RESPONSE 0x05
#define COMMAND_ACKNOWLEDGE_RESPONSE 0x07

#define NO_RESPONSE 0xFF
#define BAD_FUNCTION_CODE 0xFE
#define UNKNOWN_COMMAND 0xFD
#define RESEND_COMMAND 0xFC

#define UNCACHED_MIRROR 0xA0000000
#define PHYSICAL_MASK 0x0FFFFFFF

uint8_t *maple_base = 0;

// Debug console
char *console_base = 0;
#define console_printf(...) sprintf(console_base + strlen(console_base), __VA_ARGS__)

void display()
{
    // Render a simple test console.
    video_fill_screen(rgbto565(48, 48, 48));
    video_draw_text(0, 0, rgbto565(255, 255, 255), console_base);
    video_wait_for_vblank();
    video_display();
}

void maple_wait_for_dma()
{
    volatile unsigned int *maplebase = (volatile unsigned int *)MAPLE_BASE;

    // Wait until the MAPLE_DMA_START bit has gone back to 0.
    while((maplebase[MAPLE_DMA_START] & 1) != 0) { ; }
}

void maple_init()
{
    volatile unsigned int *maplebase = (volatile unsigned int *)MAPLE_BASE;

    // Maple init routines based on Mvc2.
    maplebase[MAPLE_DMA_HW_INIT] = (
        0x6155 << 16 |  // Security bytes
        MAPLE_ADDRESS_RANGE(0x0c000000) << 8 |  // Low address in memory where maple DMA can be found
        MAPLE_ADDRESS_RANGE(0x0dffffff)  // High address in memory where maple DMA can be found
    );
    maplebase[MAPLE_DMA_TRIGGER_SELECT] = 0;

    // Set up timeout and bitrate.
    maplebase[MAPLE_TIMEOUT_AND_SPEED] = (50000 << 16) | 0;

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

    console_printf("MAPLE_DMA_BUFFER_ADDR: %08X\n", maplebase[MAPLE_DMA_BUFFER_ADDR]);
    console_printf("MAPLE_DMA_TRIGGER_SELECT: %08X\n", maplebase[MAPLE_DMA_TRIGGER_SELECT]);
    console_printf("MAPLE_DEVICE_ENABLE: %08X\n", maplebase[MAPLE_DEVICE_ENABLE]);
    console_printf("MAPLE_DMA_START: %08X\n", maplebase[MAPLE_DMA_START]);
}

uint32_t *maple_swap_data(unsigned int port, int peripheral, unsigned int cmd, unsigned int datalen, uint32_t *data)
{
    volatile uint32_t *maplebase = (volatile uint32_t *)MAPLE_BASE;

    // First, calculate the send and receive buffers. We make sure we get a 32-byte
    // aligned address, and ensure the response buffer is in uncached memory.
    uint32_t *recv = (uint32_t *)(((((uint32_t)maple_base) + 31) & ~31) | UNCACHED_MIRROR);
    // Place the send buffer 1024 bytes after the receive buffer.
    uint32_t *send = (uint32_t *)(((uint32_t)recv) + 1024);

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
        memcpy(&send[3], data, datalen * 4);
    }

    // Set the first word of the recv buffer like real BIOS does.
    // This lets us check the response with maple_response_valid().
    memset(recv, 0, 1024);
    recv[0] = 0xFFFFFFFF;

    // Kick off the DMA request
    maple_wait_for_dma();
    maplebase[MAPLE_DMA_BUFFER_ADDR] = (uint32_t)send & PHYSICAL_MASK;
    maplebase[MAPLE_DEVICE_ENABLE] = 1;
    maplebase[MAPLE_DMA_START] = 1;

    // Wait for it to finish
    maple_wait_for_dma();

    // Return the receive buffer.
    return recv;
}

int maple_response_valid(uint32_t *response)
{
    if(response[0] == 0xFFFFFFFF)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

uint8_t maple_response_code(uint32_t *response)
{
    return response[0] & 0xFF;
}

uint8_t maple_response_length_words(uint32_t *response)
{
    return (response[0] >> 24) & 0xFF;
}

void maple_print_response(uint32_t *response)
{
    if(!maple_response_valid(response))
    {
        console_printf("Maple response is invalid.\n");
    }
    else
    {
        // Work around macro expansion bug by splitting these two up.
        console_printf("Response Code: %02X, ", maple_response_code(response));
        console_printf("Data length: %d\n", maple_response_length_words(response));

        if(maple_response_length_words(response) > 0)
        {
            console_printf("Data:");
            for (int i = 0; i < maple_response_length_words(response); i++)
            {
                console_printf(" %08X", response[i+1]);
            }
            console_printf("\n");
        }
    }
}

int maple_busy()
{
    uint32_t *resp = maple_swap_data(0, 0, DEVICE_INFO_REQUEST, 0, NULL);

    // Debug
    maple_print_response(resp);
    display();

    // MIE on Naomi doesn't respond to DEVICE_INFO_REQUEST, however it will
    // send a RESEND_COMMAND response if it is busy, and a UNKNOWN_COMMAND
    // if it is ready to go. It will return NO_RESPONSE if it is not init'd.
    // So, we check to see if either RESEND_COMMAND or NO_RESPONSE was
    // returned, and claim busy for both. We can't just check against
    // UNKNOWN_COMMAND because demul incorrectly emulates the MIE.
    uint8_t code = maple_response_code(resp);
    if (code == RESEND_COMMAND || code == NO_RESPONSE)
    {
        return 1;
    }

    return 0;
}

void maple_request_reset()
{
    console_printf("Resetting maple...\n");
    while( 1 )
    {
        uint32_t *resp = maple_swap_data(0, 0, DEVICE_RESET_REQUEST, 0, NULL);

        // Debug
        maple_print_response(resp);
        display();

        if (maple_response_code(resp) == COMMAND_ACKNOWLEDGE_RESPONSE)
        {
            break;
        }

        // Spin and try again
        for(int x = 0x2710; x > 0; x--) { ; }
    }

    console_printf("Waiting for maple to come back...\n");
    while (maple_busy())
    {
        // Spin and try again
        for(int x = 0x2710; x > 0; x--) { ; }
    }
}

void main()
{
    // Set up a crude console
    video_init_simple();
    maple_init();
    console_base = malloc(((640 * 480) / (8 * 8)) + 1);
    memset(console_base, 0, ((640 * 480) / (8 * 8)) + 1);

    // First, reset the maple HW and wait for it to settle.
    maple_request_reset();

    while ( 1 )
    {
        display();

    }
}

void test()
{
    video_init_simple();

    video_fill_screen(rgbto565(48, 48, 48));
    video_draw_text(320 - 56, 236, rgbto565(255, 255, 255), "test mode stub");
    video_wait_for_vblank();
    video_display();

    while ( 1 ) { ; }
}
