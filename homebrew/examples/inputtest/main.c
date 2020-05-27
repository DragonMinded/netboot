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
#define NAOMI_UPLOAD_CODE_REQUEST 0x80
#define NAOMI_UPLOAD_CODE_RESPONSE 0x81
#define NAOMI_VERSION_REQUEST 0x82
#define NAOMI_VERSION_RESPONSE 0x83
#define NAOMI_SELF_TEST_REQUEST 0x84
#define NAOMI_SELF_TEST_RESPONSE 0x85
#define NAOMI_IO_REQUEST 0x86
#define NAOMI_IO_RESPONSE 0x87

#define NO_RESPONSE 0xFF
#define BAD_FUNCTION_CODE 0xFE
#define UNKNOWN_COMMAND 0xFD
// Under most circumstances, an 0xFC response includes 0 words of
// data, giving no reason. However, the MIE will sometimes send a
// 1-word response. In this case, the word represents the error that
// caused an 0xFC to be generated. Those are as follows:
//
// 0x1 - Parity error on command receipt.
// 0x2 - Overflow error on command receipt.
#define RESEND_COMMAND 0xFC

// Values that get returned in various JVS packets to inform us
// whether we have a working JVS IO and whether it is addressed.
#define JVS_SENSE_DISCONNECTED 0x1
#define JVS_SENSE_ADDRESSED 0x2

#define UNCACHED_MIRROR 0xA0000000
#define PHYSICAL_MASK 0x0FFFFFFF

uint8_t *maple_base = 0;

// Debug console
char *console_base = 0;
#define console_printf(...) sprintf(console_base + strlen(console_base), __VA_ARGS__)

void display()
{
    // Render a simple test console.
    video_fill_screen(rgb(48, 48, 48));
    video_draw_text(0, 0, rgb(255, 255, 255), console_base);
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

uint32_t *maple_swap_data(unsigned int port, int peripheral, unsigned int cmd, unsigned int datalen, void *data)
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
        ((cmd & 0xFF)) |         // The command we are sending.
        ((addr & 0xFF) << 8) |   // The recipient of our packet.
        ((addr & 0xC0) << 16) |  // The sender address (us).
        ((datalen & 0xFF) << 24)  // Number of words we tack on the end.
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

uint32_t *maple_skip_response(uint32_t *response)
{
    if (!maple_response_valid(response))
    {
        return response;
    }

    return response + (1 + maple_response_length_words(response));
}

void maple_print_regs()
{
    volatile unsigned int *maplebase = (volatile unsigned int *)MAPLE_BASE;

    console_printf("MAPLE_DMA_BUFFER_ADDR: %08X\n", maplebase[MAPLE_DMA_BUFFER_ADDR]);
    console_printf("MAPLE_DMA_TRIGGER_SELECT: %08X\n", maplebase[MAPLE_DMA_TRIGGER_SELECT]);
    console_printf("MAPLE_DEVICE_ENABLE: %08X\n", maplebase[MAPLE_DEVICE_ENABLE]);
    console_printf("MAPLE_DMA_START: %08X\n", maplebase[MAPLE_DMA_START]);
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

                // We can only reasonably fit 8 words on the screen.
                if(i % 8 == 7)
                {
                    console_printf("\n     ");
                }
            }
            console_printf("\n");
        }
    }
}

/**
 * See if the MIE is busy processing a previous response or not.
 *
 * Returns 1 if the MIE is busy (can't fulfill requests) or 0 if it is
 * ready to respond to requests.
 */
int maple_busy()
{
    uint32_t *resp = maple_swap_data(0, 0, DEVICE_INFO_REQUEST, 0, NULL);

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

/**
 * Wait until the MIE is ready for commands.
 */
void maple_wait_for_ready()
{
    while (maple_busy())
    {
        // Spin and try again
        for(int x = 0x2710; x > 0; x--) { ; }
    }
}

/**
 * Request the MIE to reset all of its IO and state.
 *
 * Note that this takes awhile since the MIE needs to run memory tests. Expect
 * that this function takes upwards of a second to return. Note that after
 * executing this, you will need to re-send the custom MIE ROM image or the
 * NAOMI_IO_REQUEST handler will not be present!
 */
void maple_request_reset()
{
    while( 1 )
    {
        uint32_t *resp = maple_swap_data(0, 0, DEVICE_RESET_REQUEST, 0, NULL);
        if (maple_response_code(resp) == COMMAND_ACKNOWLEDGE_RESPONSE)
        {
            break;
        }

        // Spin and try again
        for(int x = 0x2710; x > 0; x--) { ; }
    }

    maple_wait_for_ready();
}

/**
 * Request the MIE version string embedded in the MIE ROM.
 *
 * Copies the version string to outptr. This should be at least
 * 49 bytes in length.
 */
void maple_request_version(char *outptr)
{
    uint32_t *resp;
    while( 1 )
    {
        resp = maple_swap_data(0, 0, NAOMI_VERSION_REQUEST, 0, NULL);
        if (maple_response_code(resp) == NAOMI_VERSION_RESPONSE)
        {
            break;
        }

        // Spin and try again
        for(int x = 0x2710; x > 0; x--) { ; }
    }

    // Copy the first half of the response string
    memcpy(outptr, &resp[1], maple_response_length_words(resp) * 4);
    outptr += maple_response_length_words(resp) * 4;

    // Copy the second half of the response string
    resp = maple_skip_response(resp);
    memcpy(outptr, &resp[1], maple_response_length_words(resp) * 4);
    outptr += maple_response_length_words(resp) * 4;

    // Cap it off
    outptr[0] = 0;
}

/**
 * Request the results of the power-on self-test run by the MIE.
 *
 * Returns nonzero (true) if the self-test was successful, or zero (false)
 * if the MIE reports that its RAM test has failed.
 */
int maple_request_self_test()
{
    uint32_t *resp;
    while( 1 )
    {
        resp = maple_swap_data(0, 0, NAOMI_SELF_TEST_REQUEST, 0, NULL);
        if (maple_response_code(resp) == NAOMI_SELF_TEST_RESPONSE)
        {
            break;
        }

        // Spin and try again
        for(int x = 0x2710; x > 0; x--) { ; }
    }

    if(maple_response_length_words(resp) != 1)
    {
        // This is an invalid response, consider the test failed.
        return 0;
    }

    // MIE sets this word to all 0's if the memtest passes.
    return resp[1] == 0 ? 1 : 0;
}

/**
 * Request the MIE upload a new binary and then execute it.
 *
 * Return 0 on success
 *        -1 on unexpected packet received
 *        -2 on bad memory written
 *        -3 on bad crc
 *        -4 on failure to boot code.
 */
int maple_request_update(void *binary, unsigned int len)
{
    uint8_t *binloc = (uint8_t *)binary;
    unsigned int memloc = 0x8010;
    uint32_t *resp;

    while(len > 0)
    {
        // We send in 24-byte chunks.
        uint8_t data[28];

        // First, copy the data itself over.
        memset(data, 0, 28);
        memcpy(&data[4], binloc, len > 24 ? 24 : len);

        // Now, set the address to copy to.
        data[0] = memloc & 0xFF;
        data[1] = (memloc >> 8) & 0xFF;

        // Now, calculate the checksum.
        uint8_t checksum = 0;
        for(int i = 0; i < 28; i++)
        {
            checksum = (checksum + data[i]) & 0xFF;
        }

        resp = maple_swap_data(0, 0, NAOMI_UPLOAD_CODE_REQUEST, 28 / 4, data);

        if(maple_response_code(resp) != NAOMI_UPLOAD_CODE_RESPONSE)
        {
            return -1;
        }
        if(maple_response_length_words(resp) != 0x1)
        {
            return -1;
        }
        if((resp[1] >> 16) & 0xFFFF != memloc)
        {
            return -2;
        }
        if(resp[1] & 0xFF != checksum)
        {
            return -3;
        }

        // Success! Move to next chunk
        binloc += len > 24 ? 24 : len;
        memloc += len > 24 ? 24 : len;
        len -= len > 24 ? 24 : len;
    }

    // Now, ask the MIE to execute this chunk. Technically only the first
    // two bytes need to be 0xFF (the load addr), but Naomi BIOS sends
    // all 0xFF so let's do the same.
    uint8_t execdata[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    resp = maple_swap_data(0, 0, NAOMI_UPLOAD_CODE_REQUEST, 1, execdata);
    if (maple_response_code(resp) != COMMAND_ACKNOWLEDGE_RESPONSE)
    {
        // TODO: Demul returns the wrong value here, so this code would only
        // work on an actual Naomi.
        return -4;
    }
}

/**
 * Request the MIE send a JVS command out its RS485 bus.
 *
 * Return 0 on success
 *        -1 on unexpected packet received
 */
int maple_request_send_jvs(uint8_t addr, unsigned int len, void *bytes)
{
    uint8_t subcommand[12] = {
        0x17,         // Subcommand 0x17, send simple JVS packet
        0x77,         // GPIO direction, sent in these packets for some reason?
        0x00,
        0x00,
        0x00,
        0x00,
        addr & 0xFF,  // JVS address to send to (0xFF is broadcast).
        len & 0xFF,   // Amount of data in the JVS payload.
        0x00,         // Start of data
        0x00,
        0x00,
        0x00,
    };

    memcpy(&subcommand[8], bytes, (len > 4 ? 4 : len));
    uint32_t *resp = maple_swap_data(0, 0, NAOMI_IO_REQUEST, 3, subcommand);
    if(maple_response_code(resp) != NAOMI_IO_RESPONSE)
    {
        return -1;
    }

    // We could check the JVS status in this response, as we know the valid
    // values. But, why bother?
    return 0;
}

typedef struct jvs_status
{
    uint8_t jvs_present_bitmask;
    uint8_t psw1;
    uint8_t psw2;
    uint8_t dip_switches;
    unsigned int packet_length;
    uint8_t packet[128];
} jvs_status_t;

/**
 * Request the MIE send us the response to the last JVS request.
 *
 * Returns a struct representing the JVS resp for the last request.
 */
jvs_status_t maple_request_recv_jvs()
{
    // Initialize with sane values in case we got a bad packetback.
    jvs_status_t status;
    memset(&status, 0, sizeof(status));
    status.jvs_present_bitmask = JVS_SENSE_DISCONNECTED;

    // If we request too fast after a JVS command, we might not be
    // done reading it yet!
    uint32_t *resp;
    while( 1 )
    {
        uint32_t subcommand[1] = { 0x00000015 };
        resp = maple_swap_data(0, 0, NAOMI_IO_REQUEST, 1, subcommand);
        if(maple_response_code(resp) != RESEND_COMMAND)
        {
            break;
        }
    }

    if(maple_response_code(resp) != NAOMI_IO_RESPONSE)
    {
        return status;
    }
    if(maple_response_length_words(resp) < 5)
    {
        return status;
    }

    status.dip_switches = (~(resp[2] >> 16)) & 0xF;
    status.psw1 = (~(resp[2] >> 20)) & 0x1;
    status.psw2 = (~(resp[2] >> 21)) & 0x1;
    status.jvs_present_bitmask = (resp[5] >> 16) & 0x3;

    if(maple_response_length_words(resp) >= 6)
    {
        // We have a valid packet on the end, lets grab the length first
        status.packet_length = (resp[6] >> 8) & 0xFF;
        if(status.packet_length)
        {
            memcpy(status.packet, ((uint8_t *)(&resp[6])) + 2, status.packet_length);
        }
    }

    return status;
}

void maple_print_jvs_status(jvs_status_t status)
{
    console_printf("DIPSW: %X, ", status.dip_switches);
    console_printf("PSW1: %s, ", status.psw1 ? "pressed" : "released");
    console_printf("PSW2: %s, ", status.psw2 ? "pressed" : "released");
    console_printf("JVS IO: %s, ", status.jvs_present_bitmask & JVS_SENSE_DISCONNECTED ? "disconnected" : "connected");
    console_printf("%s\n", status.jvs_present_bitmask & JVS_SENSE_ADDRESSED ? "addressed" : "unaddressed");
    if(status.packet_length)
    {
        console_printf("Response packet length: %d\n", status.packet_length);
        console_printf("Response data:");
        for(int i = 0; i < status.packet_length; i++)
        {
            console_printf(" %02X", status.packet[i]);

            // We can only reasonably fit 8 words on the screen.
            if(i % 21 == 20)
            {
                console_printf("\n              ");
            }
        }
        console_printf("\n");
    }
    else
    {
        console_printf("No response packet received.\n");
    }
}

int jvs_packet_valid(uint8_t *data)
{
    if(data[0] != 0xE0)
    {
        // Invalid SOM
        return 0;
    }

    unsigned int length = data[2] - 1;
    uint8_t expected_crc = data[3 + length];
    uint8_t calc_crc = 0;
    for(int i = 1; i < length + 3; i++)
    {
        calc_crc = (calc_crc + data[i]) & 0xFF;
    }

    if(expected_crc != calc_crc)
    {
        // Invalid CRC
        return 0;
    }

    return 1;
}

unsigned int jvs_packet_length(uint8_t *data)
{
    return data[2];
}

uint8_t *jvs_packet_payload(uint8_t *data)
{
    return data + 3;
}

/**
 * Request JVS IO at address addr to perform a reset.
 */
void maple_request_jvs_reset(uint8_t addr)
{
    // We don't bother fetching the response, much like the Naomi BIOS doesn't.
    uint8_t jvs_payload[2] = { 0xF0, 0xD9 };
    maple_request_send_jvs(addr, 2, jvs_payload);
}

/**
 * Request JVS IO at address old_addr reassign to new_addr.
 */
void maple_request_jvs_assign_address(uint8_t old_addr, uint8_t new_addr)
{
    // We don't bother fetching the response, much like the Naomi BIOS doesn't.
    uint8_t jvs_payload[2] = { 0xF1, new_addr };
    maple_request_send_jvs(old_addr, 2, jvs_payload);
}

/**
 * Request JVS IO at addr to return a version ID string.
 *
 * Return 0 on success
 *        -1 on invalid packet received
 */
int maple_request_jvs_id(uint8_t addr, char *outptr)
{
    uint8_t jvs_payload[1] = { 0x10 };
    maple_request_send_jvs(addr, 1, jvs_payload);
    jvs_status_t status = maple_request_recv_jvs();
    maple_print_jvs_status(status);
    if(!status.packet_length)
    {
        outptr[0] = 0;
        return -1;
    }

    if(!jvs_packet_valid(status.packet))
    {
        outptr[0] = 0;
        return -1;
    }

    memcpy(outptr, jvs_packet_payload(status.packet) + 2, jvs_packet_length(status.packet) - 2);
    return 0;
}

typedef struct jvs_buttons
{
} jvs_buttons_t;

/**
 * Request JVS button read from JVS ID addr and return buttons.
 */
jvs_buttons_t maple_request_jvs_buttons(uint8_t addr)
{
#if 0
    uint8_t subcommand[12] = {
        0x27,         // Subcommand 0x27, send JVS buttons packet.
        0x77,         // GPIO direction, sent in these packets for some reason?
        0x00,
        0x00,
        0x00,
        0x00,
        addr & 0xFF,   // JVS address to send to (0xFF is broadcast).
        0x01 & 0xFF,   // Amount of data in the JVS payload.
        0x00,
        0x00,
        0x00,
        0x00,
    };
#else
    uint8_t subcommand[8] = {
        0x21,         // Subcommand 0x27, send JVS buttons packet.
        0x77,         // GPIO direction, sent in these packets for some reason?
        0x00,
        0x00,
        0x00,
        0x00,
        addr & 0xFF,  // JVS address to send to (0xFF is broadcast).
        0x00,
    };
#endif

    // Set up a sane response.
    jvs_buttons_t buttons;
    memset(&buttons, 0, sizeof(buttons));

    uint32_t *resp = maple_swap_data(0, 0, NAOMI_IO_REQUEST, sizeof(subcommand) / 4, subcommand);
    if(maple_response_code(resp) != NAOMI_IO_RESPONSE)
    {
        return buttons;
    }

    jvs_status_t status = maple_request_recv_jvs();
    maple_print_jvs_status(status);

    // TODO: Parse
    return buttons;
}

void main()
{
    // Set up a crude console
    video_init_simple();
    maple_init();
    console_base = malloc(((640 * 480) / (8 * 8)) + 1);
    memset(console_base, 0, ((640 * 480) / (8 * 8)) + 1);

    // Now, report on the memory test.
    console_printf("\n\n");
    if(maple_request_self_test())
    {
        console_printf("MIE reports healthy!\n");
    }
    else
    {
        console_printf("MIE reports bad RAM!\n");
    }
    display();

    // Request version, make sure we're running our updated code.
    char version[128];
    maple_request_version(version);
    console_printf("MIE version string: %s\n", version);
    display();

    // Now, display the JVS IO version ID.
    maple_request_jvs_id(0x01, version);
    console_printf("JVS IO ID: %s\n", version);
    display();

    // Now, read the controls forever.
    unsigned int reset_loc = strlen(console_base);
    int liveness = 0;
    while ( 1 )
    {
        console_base[reset_loc] = 0;
        console_printf("Liveness indicator: %d\n", liveness++);
        jvs_buttons_t buttons = maple_request_jvs_buttons(0x01);
        display();
    }
}

void test()
{
    video_init_simple();

    video_fill_screen(rgb(48, 48, 48));
    video_draw_text(320 - 56, 236, rgb(255, 255, 255), "test mode stub");
    video_wait_for_vblank();
    video_display();

    while ( 1 ) { ; }
}
