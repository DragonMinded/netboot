#include <memory.h>
#include <stdlib.h>
#include <stdint.h>
#include "naomi/maple.h"
#include "naomi/system.h"

// Our base address for sending/receiving maple commands.
static uint8_t *maple_base = 0;

// Whether we have an outstanding JVS request to get to.
static int __outstanding_request = 0;
static unsigned int __outstanding_request_addr = 0;

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
        MAPLE_ADDRESS_RANGE(RAM_BASE) << 8 |          // Low address in memory where maple DMA can be found
        MAPLE_ADDRESS_RANGE(RAM_BASE + RAM_SIZE - 1)  // High address in memory where maple DMA can be found
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

void maple_free()
{
    free(maple_base);
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

uint8_t maple_response_payload_length_words(uint32_t *response)
{
    return (response[0] >> 24) & 0xFF;
}

uint32_t *maple_skip_response(uint32_t *response)
{
    if (!maple_response_valid(response))
    {
        return response;
    }

    return response + (1 + maple_response_payload_length_words(response));
}

/**
 * See if the MIE is busy processing a previous response or not.
 *
 * Returns 1 if the MIE is busy (can't fulfill requests) or 0 if it is
 * ready to respond to requests.
 */
int maple_busy()
{
    uint32_t *resp = maple_swap_data(0, 0, MAPLE_DEVICE_INFO_REQUEST, 0, NULL);

    // MIE on Naomi doesn't respond to MAPLE_DEVICE_INFO_REQUEST, however it will
    // send a MAPLE_RESEND_COMMAND response if it is busy, and a UNKNOWN_COMMAND
    // if it is ready to go. It will return MAPLE_NO_RESPONSE if it is not init'd.
    // So, we check to see if either MAPLE_RESEND_COMMAND or MAPLE_NO_RESPONSE was
    // returned, and claim busy for both. We can't just check against
    // UNKNOWN_COMMAND because demul incorrectly emulates the MIE.
    uint8_t code = maple_response_code(resp);
    if (code == MAPLE_RESEND_COMMAND || code == MAPLE_NO_RESPONSE)
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
 * MAPLE_NAOMI_IO_REQUEST handler will not be present!
 */
void maple_request_reset()
{
    while( 1 )
    {
        uint32_t *resp = maple_swap_data(0, 0, MAPLE_DEVICE_RESET_REQUEST, 0, NULL);
        if (maple_response_code(resp) == MAPLE_COMMAND_ACKNOWLEDGE_RESPONSE)
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
        resp = maple_swap_data(0, 0, MAPLE_NAOMI_VERSION_REQUEST, 0, NULL);
        if (maple_response_code(resp) == MAPLE_NAOMI_VERSION_RESPONSE)
        {
            break;
        }

        // Spin and try again
        for(int x = 0x2710; x > 0; x--) { ; }
    }

    // Copy the first half of the response string
    memcpy(outptr, &resp[1], maple_response_payload_length_words(resp) * 4);
    outptr += maple_response_payload_length_words(resp) * 4;

    // Copy the second half of the response string
    resp = maple_skip_response(resp);
    memcpy(outptr, &resp[1], maple_response_payload_length_words(resp) * 4);
    outptr += maple_response_payload_length_words(resp) * 4;

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
        resp = maple_swap_data(0, 0, MAPLE_NAOMI_SELF_TEST_REQUEST, 0, NULL);
        if (maple_response_code(resp) == MAPLE_NAOMI_SELF_TEST_RESPONSE)
        {
            break;
        }

        // Spin and try again
        for(int x = 0x2710; x > 0; x--) { ; }
    }

    if(maple_response_payload_length_words(resp) != 1)
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

        resp = maple_swap_data(0, 0, MAPLE_NAOMI_UPLOAD_CODE_REQUEST, 28 / 4, data);

        if(maple_response_code(resp) != MAPLE_NAOMI_UPLOAD_CODE_RESPONSE)
        {
            return -1;
        }
        if(maple_response_payload_length_words(resp) != 0x1)
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
    resp = maple_swap_data(0, 0, MAPLE_NAOMI_UPLOAD_CODE_REQUEST, 1, execdata);
    if (maple_response_code(resp) != MAPLE_COMMAND_ACKNOWLEDGE_RESPONSE)
    {
        // TODO: A different value is returned by different revisions of the
        // MIE which depend on the Naomi BIOS. However, since netboot only
        // works on Rev. H BIOS, I think we're good here.
        return -4;
    }
}

/**
 * Request the EEPROM contents from the MIE.
 *
 * Returns 0 on success
 *         -1 on unexpected packet received
 */
int maple_request_eeprom_read(uint8_t *outbytes)
{
    uint8_t req_subcommand[4] = {
        0x01,         // Subcommand 0x01, read whole EEPROM to MIE.
        0x00,
        0x00,
        0x00,
    };

    uint32_t *resp = maple_swap_data(0, 0, MAPLE_NAOMI_IO_REQUEST, 1, req_subcommand);
    if(maple_response_code(resp) != MAPLE_NAOMI_IO_RESPONSE)
    {
        // Invalid response packet
        return -1;
    }
    if(maple_response_payload_length_words(resp) < 1)
    {
        // Invalid payload length. We would check against exactly 1 word, but
        // it looks like sometimes the MIE responds with 2 words.
        return -1;
    }
    if(resp[1] != 0x02)
    {
        // Invalid subcommand response
        return -1;
    }

    // Now, wait until the EEPROM is read to fetch it.
    maple_wait_for_ready();

    uint8_t fetch_subcommand[4] = {
        0x03,         // Subcommand 0x03, read EEPROM result.
        0x00,
        0x00,
        0x00,
    };

    resp = maple_swap_data(0, 0, MAPLE_NAOMI_IO_REQUEST, 1, fetch_subcommand);
    if(maple_response_code(resp) != MAPLE_NAOMI_IO_RESPONSE)
    {
        // Invalid response packet
        return -1;
    }
    if(maple_response_payload_length_words(resp) != 32)
    {
        // Invalid payload length
        return -1;
    }

    // Copy the data out, we did it!
    memcpy(outbytes, &resp[1], 128);
    return 0;
}

/**
 * Request writing a new EEPROM contents to the MIE.
 *
 * Returns 0 on success
 *         -1 on unexpected packet received
 */
int maple_request_eeprom_write(uint8_t *inbytes)
{
    for(unsigned int i = 0; i < 0x80; i += 0x10)
    {
        // First, craft the subcommand requesting an EEPROM chunk write.
        uint8_t req_subcommand[20];
        req_subcommand[0] = 0x0B;      // Subcommand 0x0B, write chunk of EEPROM.
        req_subcommand[1] = i & 0xFF;  // Write offset, relative to start of EEPROM.
        req_subcommand[2] = 0x10;      // Chunk size, always 0x10 in practice.
        req_subcommand[3] = 0x00;
        memcpy(&req_subcommand[4], &inbytes[i], 0x10);

        // Now, send it, verifying that it acknowledged the data
        uint32_t *resp = maple_swap_data(0, 0, MAPLE_NAOMI_IO_REQUEST, 5, req_subcommand);
        if(maple_response_code(resp) != MAPLE_NAOMI_IO_RESPONSE)
        {
            // Invalid response packet
            return -1;
        }

        // Now, wait for the write operation to finish.
        maple_wait_for_ready();
    }

    // Succeeded in writing new EEPROM!
    return 0;
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
    uint32_t *resp = maple_swap_data(0, 0, MAPLE_NAOMI_IO_REQUEST, 3, subcommand);
    if(maple_response_code(resp) != MAPLE_NAOMI_IO_RESPONSE)
    {
        return -1;
    }

    // We could check the JVS status in this response, as we know the valid
    // values. But, why bother?
    return 0;
}

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
        resp = maple_swap_data(0, 0, MAPLE_NAOMI_IO_REQUEST, 1, subcommand);
        if(maple_response_code(resp) != MAPLE_RESEND_COMMAND)
        {
            break;
        }
    }

    if(maple_response_code(resp) != MAPLE_NAOMI_IO_RESPONSE)
    {
        return status;
    }
    if(maple_response_payload_length_words(resp) < 5)
    {
        return status;
    }

    status.dip_switches = (~(resp[2] >> 16)) & 0xF;
    status.psw1 = (~(resp[2] >> 20)) & 0x1;
    status.psw2 = (~(resp[2] >> 21)) & 0x1;
    status.jvs_present_bitmask = (resp[5] >> 16) & 0x3;

    if(maple_response_payload_length_words(resp) >= 6)
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

unsigned int jvs_packet_payload_length_bytes(uint8_t *data)
{
    return data[2] - 1;
}

unsigned int jvs_packet_code(uint8_t *data)
{
    return data[3];
}

uint8_t *jvs_packet_payload(uint8_t *data)
{
    return data + 4;
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
    if(!status.packet_length)
    {
        // Didn't get a packet
        outptr[0] = 0;
        return -1;
    }

    if(!jvs_packet_valid(status.packet))
    {
        // Packet failed CRC
        outptr[0] = 0;
        return -1;
    }

    if(jvs_packet_code(status.packet) != 0x01)
    {
        // Packet is not response type.
        outptr[0] = 0;
        return -1;
    }
    if(jvs_packet_payload(status.packet)[0] != 0x01)
    {
        // Packet is not report type.
        outptr[0] = 0;
        return -1;
    }

    memcpy(outptr, jvs_packet_payload(status.packet) + 1, jvs_packet_payload_length_bytes(status.packet) - 1);
    return 0;
}

int __maple_request_jvs_send_buttons_packet(uint8_t addr, unsigned int unknown)
{
    uint8_t subcommand[12] = {
        0x27,                  // Subcommand 0x27, send JVS buttons packet.
        0x77,                  // GPIO direction, sent in these packets for some reason?
        0x00,
        0x00,
        0x00,
        0x00,
        addr & 0xFF,           // JVS address to send to (0xFF is broadcast).
        unknown & 0xFF,        // I thought this was player count but I am wrong, not sure now.
        0x00,
        0x00,
        0x00,
        0x00,
    };

    uint32_t *resp = maple_swap_data(0, 0, MAPLE_NAOMI_IO_REQUEST, sizeof(subcommand) / 4, subcommand);
    if(maple_response_code(resp) != MAPLE_NAOMI_IO_RESPONSE)
    {
        return 0;
    }

    return 1;
}

/**
 * Request JVS button read from JVS ID addr and return buttons.
 */
jvs_buttons_t maple_request_jvs_buttons(uint8_t addr)
{
    // Set up a sane response.
    jvs_buttons_t buttons;
    memset(&buttons, 0, sizeof(buttons));

    if (!__outstanding_request || __outstanding_request_addr != addr)
    {
        if(!__maple_request_jvs_send_buttons_packet(addr, 1))
        {
            // Didn't get a valid response for sending JVS.
            return buttons;
        }
    }

    jvs_status_t status = maple_request_recv_jvs();
    if(!status.packet_length)
    {
        // Didn't get a packet
        return buttons;
    }

    if(!jvs_packet_valid(status.packet))
    {
        // Packet failed CRC
        return buttons;
    }

    if(jvs_packet_code(status.packet) != 0x01)
    {
        // Packet is not response type.
        return buttons;
    }
    if(jvs_packet_payload(status.packet)[0] != 0x01)
    {
        // Packet is not report type.
        return buttons;
    }

    // Parse out the buttons
    uint8_t *payload = jvs_packet_payload(status.packet) + 1;
    buttons.dip1 = status.dip_switches & 0x1;
    buttons.dip2 = (status.dip_switches >> 1) & 0x1;
    buttons.dip3 = (status.dip_switches >> 2) & 0x1;
    buttons.dip4 = (status.dip_switches >> 3) & 0x1;
    buttons.psw1 = status.psw1;
    buttons.psw2 = status.psw2;
    buttons.test = (payload[0] >> 7) & 0x1;

    // Player 1 controls
    buttons.player1.service = (payload[1] >> 6) & 0x1;
    buttons.player1.start = (payload[1] >> 7) & 0x1;
    buttons.player1.up = (payload[1] >> 5) & 0x1;
    buttons.player1.down = (payload[1] >> 4) & 0x1;
    buttons.player1.left = (payload[1] >> 3) & 0x1;
    buttons.player1.right = (payload[1] >> 2) & 0x1;
    buttons.player1.button1 = (payload[1] >> 1) & 0x1;
    buttons.player1.button2 = payload[1] & 0x1;
    buttons.player1.button3 = (payload[2] >> 7) & 0x1;
    buttons.player1.button4 = (payload[2] >> 6) & 0x1;
    buttons.player1.button5 = (payload[2] >> 5) & 0x1;
    buttons.player1.button6 = (payload[2] >> 4) & 0x1;
    buttons.player1.analog1 = payload[11];
    buttons.player1.analog2 = payload[13];
    buttons.player1.analog3 = payload[15];
    buttons.player1.analog4 = payload[17];

    // Player 2 controls
    buttons.player2.service = (payload[3] >> 6) & 0x1;
    buttons.player2.start = (payload[3] >> 7) & 0x1;
    buttons.player2.up = (payload[3] >> 5) & 0x1;
    buttons.player2.down = (payload[3] >> 4) & 0x1;
    buttons.player2.left = (payload[3] >> 3) & 0x1;
    buttons.player2.right = (payload[3] >> 2) & 0x1;
    buttons.player2.button1 = (payload[3] >> 1) & 0x1;
    buttons.player2.button2 = payload[3] & 0x1;
    buttons.player2.button3 = (payload[4] >> 7) & 0x1;
    buttons.player2.button4 = (payload[4] >> 6) & 0x1;
    buttons.player2.button5 = (payload[4] >> 5) & 0x1;
    buttons.player2.button6 = (payload[4] >> 4) & 0x1;
    buttons.player2.analog1 = payload[19];
    buttons.player2.analog2 = payload[21];
    buttons.player2.analog3 = payload[23];
    buttons.player2.analog4 = payload[25];

    // Finally, send another request to be ready next time we poll.
    __outstanding_request = __maple_request_jvs_send_buttons_packet(addr, 1);
    __outstanding_request_addr = addr;
    return buttons;
}
