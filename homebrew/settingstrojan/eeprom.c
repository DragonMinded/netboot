#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "eeprom.h"

uint32_t eeprom_crc_inner(uint32_t running_crc, uint8_t next_byte)
{
    // First, mask off the values so we don't get a collision
    running_crc &= 0xFFFFFF00;

    // Add the byte into the CRC
    running_crc = running_crc | next_byte;

    // Now, run the algorithm across the new byte
    for (int i = 0; i < 8; i++)
    {
        if (running_crc < 0x80000000)
        {
            running_crc = running_crc << 1;
        }
        else
        {
            running_crc = (running_crc << 1) + 0x10210000;
        }
    }

    return running_crc;
}

uint16_t eeprom_crc(uint8_t *data, unsigned int len)
{
    uint32_t running_crc = 0xDEBDEB00;

    // CRC over all the data we've been given.
    for (unsigned int i = 0; i < len; i++)
    {
        running_crc = eeprom_crc_inner(running_crc, data[i]);
    }

    // Add in the null byte that Naomi BIOS seems to want.
    running_crc = eeprom_crc_inner(running_crc, 0);

    // Calculate the final CRC value by taking the remainder.
    return (running_crc >> 16) & 0xFFFF;
}

int eeprom_system_valid(uint8_t *data)
{
    uint16_t expected = 0;

    // Calculate first system chunk.
    memcpy(&expected, data + SYSTEM_CHUNK_1 + SYSTEM_CRC_LOC, SYSTEM_CRC_SIZE);
    if (expected != eeprom_crc(data + SYSTEM_CHUNK_1 + SYSTEM_CRC_REGION_LOC, SYSTEM_CRC_REGION_SIZE))
    {
        return 0;
    }

    // Calculate second system chunk.
    memcpy(&expected, data + SYSTEM_CHUNK_2 + SYSTEM_CRC_LOC, SYSTEM_CRC_SIZE);
    if (expected != eeprom_crc(data + SYSTEM_CHUNK_2 + SYSTEM_CRC_REGION_LOC, SYSTEM_CRC_REGION_SIZE))
    {
        return 0;
    }

    // All CRCs passed!
    return 1;
}

int eeprom_game_valid(uint8_t *data)
{
    uint16_t expected = 0;

    // Verify the lengths are correct.
    if (data[GAME_CHUNK_1 + GAME_LEN_LOC_1] != data[GAME_CHUNK_1 + GAME_LEN_LOC_2])
    {
        return 0;
    }
    if (data[GAME_CHUNK_2 + GAME_LEN_LOC_1] != data[GAME_CHUNK_2 + GAME_LEN_LOC_2])
    {
        return 0;
    }

    // Calculate first game chunk.
    memcpy(&expected, data + GAME_CHUNK_1 + GAME_CRC_LOC, GAME_CRC_SIZE);
    if (expected != eeprom_crc(data + GAME_PAYLOAD, data[GAME_CHUNK_1 + GAME_LEN_LOC_1]))
    {
        return 0;
    }

    // Calculate second game chunk.
    memcpy(&expected, data + GAME_CHUNK_2 + GAME_CRC_LOC, GAME_CRC_SIZE);
    if (expected != eeprom_crc(data + GAME_PAYLOAD + data[GAME_CHUNK_1 + GAME_LEN_LOC_1], data[GAME_CHUNK_2 + GAME_LEN_LOC_1]))
    {
        return 0;
    }

    // All CRCs passed!
    return 1;
}

#define UNCACHED_MIRROR 0xA0000000
#define PHYSICAL_MASK 0x0FFFFFFF

#define RAM_BASE 0x0c000000
#define RAM_SIZE 0x2000000

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

#define MAPLE_ADDRESS_RANGE(x) (((x) >> 20) - 0x80)

#define MAPLE_DEVICE_INFO_REQUEST 0x01
#define MAPLE_DEVICE_RESET_REQUEST 0x03
#define MAPLE_DEVICE_INFO_RESPONSE 0x05
#define MAPLE_COMMAND_ACKNOWLEDGE_RESPONSE 0x07
#define MAPLE_NAOMI_UPLOAD_CODE_REQUEST 0x80
#define MAPLE_NAOMI_UPLOAD_CODE_RESPONSE 0x80
#define MAPLE_NAOMI_UPLOAD_CODE_BOOTUP_RESPONSE 0x81
#define MAPLE_NAOMI_VERSION_REQUEST 0x82
#define MAPLE_NAOMI_VERSION_RESPONSE 0x83
#define MAPLE_NAOMI_SELF_TEST_REQUEST 0x84
#define MAPLE_NAOMI_SELF_TEST_RESPONSE 0x85
#define MAPLE_NAOMI_IO_REQUEST 0x86
#define MAPLE_NAOMI_IO_RESPONSE 0x87

#define MAPLE_NO_RESPONSE 0xFF
#define MAPLE_BAD_FUNCTION_CODE 0xFE
#define MAPLE_UNKNOWN_COMMAND 0xFD

// Under most circumstances, an 0xFC response includes 0 words of
// data, giving no reason. However, the MIE will sometimes send a
// 1-word response. In this case, the word represents the error that
// caused an 0xFC to be generated. Those are as follows:
//
// 0x1 - Parity error on command receipt.
// 0x2 - Overflow error on command receipt.
#define MAPLE_RESEND_COMMAND 0xFC

// Our base address for sending/receiving maple commands.
static uint8_t maple_base[1024 + 1024 + 32];

void _maple_wait_for_dma()
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
    _maple_wait_for_dma();
}

void maple_free()
{
    // Nothing.
}

uint32_t *_maple_swap_data(unsigned int port, int peripheral, unsigned int cmd, unsigned int datalen, void *data)
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
        addr = ((port & 0x3) << 6) | 0x20;
    }
    else
    {
        // Sub peripheral.
        addr = ((port & 0x3) << 6) | ((1 << (peripheral - 1)) & 0x1F);
    }

    // Calculate receive buffer
    uint32_t buffer = (uint32_t)recv & PHYSICAL_MASK;

    // Wait until any transfer finishes before messing with memory, then point at
    // our buffer.
    _maple_wait_for_dma();

    // Now, construct the maple request transfer descriptor.
    send[0] = (
        1 << 31 |               // This is the last entry in the transfer descriptor.
        ((port & 0x3) << 16) |  // Set DMA port as well
        (datalen & 0xFF)        // Length is how many extra bytes of payload we are including.
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
    // This lets us check the response with _maple_response_valid().
    recv[0] = 0xFFFFFFFF;

    // Kick off the DMA request
    _maple_wait_for_dma();
    maplebase[MAPLE_DMA_BUFFER_ADDR] = (uint32_t)send & PHYSICAL_MASK;
    maplebase[MAPLE_DEVICE_ENABLE] = 1;
    maplebase[MAPLE_DMA_START] = 1;

    // Wait for it to finish
    _maple_wait_for_dma();

    // Return the receive buffer.
    return recv;
}

int _maple_response_valid(uint32_t *response)
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

uint8_t _maple_response_code(uint32_t *response)
{
    return response[0] & 0xFF;
}

uint8_t _maple_response_payload_length_words(uint32_t *response)
{
    return (response[0] >> 24) & 0xFF;
}

uint32_t *_maple_skip_response(uint32_t *response)
{
    if (!_maple_response_valid(response))
    {
        return response;
    }

    return response + (1 + _maple_response_payload_length_words(response));
}

int _maple_busy()
{
    uint32_t *resp = _maple_swap_data(0, 0, MAPLE_DEVICE_INFO_REQUEST, 0, NULL);

    // MIE on Naomi doesn't respond to MAPLE_DEVICE_INFO_REQUEST, however it will
    // send a MAPLE_RESEND_COMMAND response if it is busy, and a UNKNOWN_COMMAND
    // if it is ready to go. It will return MAPLE_NO_RESPONSE if it is not init'd.
    // So, we check to see if either MAPLE_RESEND_COMMAND or MAPLE_NO_RESPONSE was
    // returned, and claim busy for both. We can't just check against
    // UNKNOWN_COMMAND because demul incorrectly emulates the MIE.
    uint8_t code = _maple_response_code(resp);
    if (code == MAPLE_RESEND_COMMAND || code == MAPLE_NO_RESPONSE)
    {
        return 1;
    }

    return 0;
}

/**
 * Wait until the MIE is ready for commands.
 */
void _maple_wait_for_ready()
{
    while (_maple_busy())
    {
        // Spin and try again
        for(volatile int x = 10000; x > 0; x--) { ; }
    }
}

int maple_request_eeprom_read(uint8_t *outbytes)
{
    uint8_t req_subcommand[4] = {
        0x01,         // Subcommand 0x01, read whole EEPROM to MIE.
        0x00,
        0x00,
        0x00,
    };

    uint32_t *resp = _maple_swap_data(0, 0, MAPLE_NAOMI_IO_REQUEST, 1, req_subcommand);
    if(_maple_response_code(resp) != MAPLE_NAOMI_IO_RESPONSE)
    {
        // Invalid response packet
        return -1;
    }
    if(_maple_response_payload_length_words(resp) < 1)
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
    _maple_wait_for_ready();

    uint8_t fetch_subcommand[4] = {
        0x03,         // Subcommand 0x03, read EEPROM result.
        0x00,
        0x00,
        0x00,
    };

    resp = _maple_swap_data(0, 0, MAPLE_NAOMI_IO_REQUEST, 1, fetch_subcommand);
    if(_maple_response_code(resp) != MAPLE_NAOMI_IO_RESPONSE)
    {
        // Invalid response packet
        return -1;
    }
    if(_maple_response_payload_length_words(resp) != 32)
    {
        // Invalid payload length
        return -1;
    }

    // Copy the data out, we did it!
    memcpy(outbytes, &resp[1], 128);
    return 0;
}

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
        uint32_t *resp = _maple_swap_data(0, 0, MAPLE_NAOMI_IO_REQUEST, 5, req_subcommand);
        if(_maple_response_code(resp) != MAPLE_NAOMI_IO_RESPONSE)
        {
            // Invalid response packet
            return -1;
        }

        // Now, wait for the write operation to finish.
        _maple_wait_for_ready();
    }

    return 0;
}
