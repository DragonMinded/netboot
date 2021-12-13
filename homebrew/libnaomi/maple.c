#include <memory.h>
#include <stdlib.h>
#include <stdint.h>
#include "naomi/maple.h"
#include "naomi/system.h"
#include "naomi/thread.h"
#include "naomi/interrupt.h"
#include "irqinternal.h"

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

// Values that get returned in various JVS packets to inform us
// whether we have a working JVS IO and whether it is addressed.
#define JVS_SENSE_DISCONNECTED 0x1
#define JVS_SENSE_ADDRESSED 0x2

typedef struct jvs_status
{
    uint8_t jvs_present_bitmask;
    uint8_t psw1;
    uint8_t psw2;
    uint8_t dip_switches;
    unsigned int packet_length;
    uint8_t packet[128];
} jvs_status_t;

// Our base address for sending/receiving maple commands.
static uint8_t *maple_base = 0;

// Whether we have an outstanding JVS request to get to.
static int __outstanding_request = 0;
static unsigned int __outstanding_request_addr = 0;

// Last and current poll results for maple buttons.
static jvs_buttons_t last_buttons;
static jvs_buttons_t cur_buttons;
static int first_poll = 0;

/* Global hardware access mutexes. */
static mutex_t maple_mutex;

void _maple_wait_for_dma()
{
    volatile unsigned int *maplebase = (volatile unsigned int *)MAPLE_BASE;

    // Wait until the MAPLE_DMA_START bit has gone back to 0.
    while((maplebase[MAPLE_DMA_START] & 1) != 0) { ; }
}

void _maple_init()
{
    volatile unsigned int *maplebase = (volatile unsigned int *)MAPLE_BASE;
    uint32_t old_interrupts = irq_disable();

    // Reset button polling API.
    memset(&last_buttons, 0, sizeof(last_buttons));
    memset(&cur_buttons, 0, sizeof(cur_buttons));
    first_poll = 0;

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

    // Allocate enough memory for a request and a response, as well as
    // 32 bytes of padding.
    maple_base = malloc(1024 + 1024 + 32);
    if (maple_base == 0)
    {
        _irq_display_invariant("memory failure", "could not get memory for maple exchange buffer!");
    }

    // Alow ourselves exclusive access to the hardware.
    mutex_init(&maple_mutex);
    irq_restore(old_interrupts);
}

void _maple_free()
{
    // Do the reverse of the above init.
    uint32_t old_interrupts = irq_disable();
    mutex_free(&maple_mutex);
    free(maple_base);
    maple_base = 0;
    irq_restore(old_interrupts);
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

/**
 * See if the MIE is busy processing a previous response or not.
 *
 * Returns 1 if the MIE is busy (can't fulfill requests) or 0 if it is
 * ready to respond to requests.
 */
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

/**
 * Request the MIE to reset all of its IO and state.
 *
 * Note that this takes awhile since the MIE needs to run memory tests. Expect
 * that this function takes upwards of a second to return. Note that after
 * executing this, you will need to re-send the custom MIE ROM image or the
 * MAPLE_NAOMI_IO_REQUEST handler will not be present! Returns 0 on success
 * or a negative value to indicate we could not access the hardware.
 */
int maple_request_reset()
{
    if (mutex_try_lock(&maple_mutex))
    {
        while( 1 )
        {
            uint32_t *resp = _maple_swap_data(0, 0, MAPLE_DEVICE_RESET_REQUEST, 0, NULL);
            if (_maple_response_code(resp) == MAPLE_COMMAND_ACKNOWLEDGE_RESPONSE)
            {
                break;
            }

            // Spin and try again
            for(volatile int x = 10000; x > 0; x--) { ; }
        }

        _maple_wait_for_ready();
        mutex_unlock(&maple_mutex);
        return 0;
    }

    return -1;
}

/**
 * Request the MIE version string embedded in the MIE ROM.
 *
 * Copies the version string to outptr. This should be at least
 * 49 bytes in length. Returns 0 on success or a negative value
 * to signify that we could not access the hardware.
 */
int maple_request_version(char *outptr)
{
    if (mutex_try_lock(&maple_mutex))
    {
        uint32_t *resp;
        while( 1 )
        {
            resp = _maple_swap_data(0, 0, MAPLE_NAOMI_VERSION_REQUEST, 0, NULL);
            if (_maple_response_code(resp) == MAPLE_NAOMI_VERSION_RESPONSE)
            {
                break;
            }

            // Spin and try again
            for(volatile int x = 10000; x > 0; x--) { ; }
        }

        // Copy the first half of the response string
        memcpy(outptr, &resp[1], _maple_response_payload_length_words(resp) * 4);
        outptr += _maple_response_payload_length_words(resp) * 4;

        // Copy the second half of the response string
        resp = _maple_skip_response(resp);
        memcpy(outptr, &resp[1], _maple_response_payload_length_words(resp) * 4);
        outptr += _maple_response_payload_length_words(resp) * 4;

        // Cap it off
        outptr[0] = 0;
        mutex_unlock(&maple_mutex);
        return 0;
    }

    return -1;
}

/**
 * Request the results of the power-on self-test run by the MIE.
 *
 * Returns nonzero (true) if the self-test was successful, zero (false)
 * if the MIE reports that its RAM test has failed, and negative if we
 * could not get access to the hardware.
 */
int maple_request_self_test()
{
    if (mutex_try_lock(&maple_mutex))
    {
        uint32_t *resp;
        while( 1 )
        {
            resp = _maple_swap_data(0, 0, MAPLE_NAOMI_SELF_TEST_REQUEST, 0, NULL);
            if (_maple_response_code(resp) == MAPLE_NAOMI_SELF_TEST_RESPONSE)
            {
                break;
            }

            // Spin and try again
            for(volatile int x = 10000; x > 0; x--) { ; }
        }

        if(_maple_response_payload_length_words(resp) != 1)
        {
            // This is an invalid response, consider the test failed.
            mutex_unlock(&maple_mutex);
            return 0;
        }

        // MIE sets this word to all 0's if the memtest passes.
        mutex_unlock(&maple_mutex);
        return resp[1] == 0 ? 1 : 0;
    }

    return -1;
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
    if (mutex_try_lock(&maple_mutex))
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
            data[3] = memloc & 0xFF;
            data[2] = (memloc >> 8) & 0xFF;

            // Now, calculate the checksum.
            uint8_t checksum = 0;
            for(int i = 0; i < 28; i++)
            {
                checksum = (checksum + data[i]) & 0xFF;
            }

            resp = _maple_swap_data(0, 0, MAPLE_NAOMI_UPLOAD_CODE_REQUEST, 28 / 4, data);

            if(_maple_response_code(resp) != MAPLE_NAOMI_UPLOAD_CODE_RESPONSE && (_maple_response_code(resp) != MAPLE_NAOMI_UPLOAD_CODE_BOOTUP_RESPONSE))
            {
                mutex_unlock(&maple_mutex);
                return -1;
            }
            if(_maple_response_payload_length_words(resp) != 0x1)
            {
                mutex_unlock(&maple_mutex);
                return -1;
            }
            if((((resp[1] & 0xFF0000) >> 8) | ((resp[1] & 0xFF000000) >> 24)) != memloc)
            {
                mutex_unlock(&maple_mutex);
                return -2;
            }
            if((resp[1] & 0xFF) != checksum)
            {
                mutex_unlock(&maple_mutex);
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
        resp = _maple_swap_data(0, 0, MAPLE_NAOMI_UPLOAD_CODE_REQUEST, 1, execdata);
        if (_maple_response_code(resp) != MAPLE_COMMAND_ACKNOWLEDGE_RESPONSE)
        {
            // A different value is returned by different revisions of the
            // MIE which depend on the Naomi BIOS. However, since netboot only
            // works on Rev. H BIOS, I think we're good not supporting them
            // here.
            mutex_unlock(&maple_mutex);
            return -4;
        }

        mutex_unlock(&maple_mutex);
        return 0;
    }

    return -5;
}

/**
 * Request the EEPROM contents from the MIE.
 *
 * Returns 0 on success
 *         -1 on unexpected packet received or failed to lock hardware
 */
int maple_request_eeprom_read(uint8_t *outbytes)
{
    if (mutex_try_lock(&maple_mutex))
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
            mutex_unlock(&maple_mutex);
            return -1;
        }
        if(_maple_response_payload_length_words(resp) < 1)
        {
            // Invalid payload length. We would check against exactly 1 word, but
            // it looks like sometimes the MIE responds with 2 words.
            mutex_unlock(&maple_mutex);
            return -1;
        }
        if(resp[1] != 0x02)
        {
            // Invalid subcommand response
            mutex_unlock(&maple_mutex);
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
            mutex_unlock(&maple_mutex);
            return -1;
        }
        if(_maple_response_payload_length_words(resp) != 32)
        {
            // Invalid payload length
            mutex_unlock(&maple_mutex);
            return -1;
        }

        // Copy the data out, we did it!
        memcpy(outbytes, &resp[1], 128);
        mutex_unlock(&maple_mutex);
        return 0;
    }

    return -1;
}

/**
 * Request writing a new EEPROM contents to the MIE.
 *
 * Returns 0 on success
 *         -1 on unexpected packet received or failed to lock hardware
 */
int maple_request_eeprom_write(uint8_t *inbytes)
{
    if (mutex_try_lock(&maple_mutex))
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
                mutex_unlock(&maple_mutex);
                return -1;
            }

            // Now, wait for the write operation to finish.
            _maple_wait_for_ready();
        }

        // Succeeded in writing new EEPROM!
        mutex_unlock(&maple_mutex);
        return 0;
    }

    return -1;
}

/**
 * Request the MIE send a JVS command out its RS485 bus.
 *
 * Return 0 on success
 *        -1 on unexpected packet received
 */
int _maple_request_send_jvs(uint8_t addr, unsigned int len, void *bytes)
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
    uint32_t *resp = _maple_swap_data(0, 0, MAPLE_NAOMI_IO_REQUEST, 3, subcommand);
    if(_maple_response_code(resp) != MAPLE_NAOMI_IO_RESPONSE)
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
jvs_status_t _maple_request_recv_jvs()
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
        resp = _maple_swap_data(0, 0, MAPLE_NAOMI_IO_REQUEST, 1, subcommand);
        if(_maple_response_code(resp) != MAPLE_RESEND_COMMAND)
        {
            break;
        }
    }

    if(_maple_response_code(resp) != MAPLE_NAOMI_IO_RESPONSE)
    {
        return status;
    }
    if(_maple_response_payload_length_words(resp) < 5)
    {
        return status;
    }

    status.dip_switches = (~(resp[2] >> 16)) & 0xF;
    status.psw1 = (~(resp[2] >> 20)) & 0x1;
    status.psw2 = (~(resp[2] >> 21)) & 0x1;
    status.jvs_present_bitmask = (resp[5] >> 16) & 0x3;

    if(_maple_response_payload_length_words(resp) >= 6)
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

int _jvs_packet_valid(uint8_t *data)
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

unsigned int _jvs_packet_payload_length_bytes(uint8_t *data)
{
    return data[2] - 1;
}

unsigned int _jvs_packet_code(uint8_t *data)
{
    return data[3];
}

uint8_t *_jvs_packet_payload(uint8_t *data)
{
    return data + 4;
}

/**
 * Request JVS IO at address addr to perform a reset.
 */
int maple_request_jvs_reset(uint8_t addr)
{
    if (mutex_try_lock(&maple_mutex))
    {
        // We don't bother fetching the response, much like the Naomi BIOS doesn't.
        uint8_t jvs_payload[2] = { 0xF0, 0xD9 };
        _maple_request_send_jvs(addr, 2, jvs_payload);

        mutex_unlock(&maple_mutex);
        return 0;
    }

    return -1;
}

/**
 * Request JVS IO at address old_addr reassign to new_addr.
 */
int maple_request_jvs_assign_address(uint8_t old_addr, uint8_t new_addr)
{
    if (mutex_try_lock(&maple_mutex))
    {
        // We don't bother fetching the response, much like the Naomi BIOS doesn't.
        uint8_t jvs_payload[2] = { 0xF1, new_addr };
        _maple_request_send_jvs(old_addr, 2, jvs_payload);

        mutex_unlock(&maple_mutex);
        return 0;
    }

    return -1;
}

/**
 * Request JVS IO at addr to return a version ID string.
 *
 * Return 0 on success
 *        -1 on invalid packet received or couldn't lock the hardware.
 */
int maple_request_jvs_id(uint8_t addr, char *outptr)
{
    if (mutex_try_lock(&maple_mutex))
    {
        uint8_t jvs_payload[1] = { 0x10 };
        _maple_request_send_jvs(addr, 1, jvs_payload);
        jvs_status_t status = _maple_request_recv_jvs();
        if(!status.packet_length)
        {
            // Didn't get a packet
            outptr[0] = 0;
            mutex_unlock(&maple_mutex);
            return -1;
        }

        if(!_jvs_packet_valid(status.packet))
        {
            // Packet failed CRC
            outptr[0] = 0;
            mutex_unlock(&maple_mutex);
            return -1;
        }

        if(_jvs_packet_code(status.packet) != 0x01)
        {
            // Packet is not response type.
            outptr[0] = 0;
            mutex_unlock(&maple_mutex);
            return -1;
        }
        if(_jvs_packet_payload(status.packet)[0] != 0x01)
        {
            // Packet is not report type.
            outptr[0] = 0;
            mutex_unlock(&maple_mutex);
            return -1;
        }

        memcpy(outptr, _jvs_packet_payload(status.packet) + 1, _jvs_packet_payload_length_bytes(status.packet) - 1);
        mutex_unlock(&maple_mutex);
        return 0;
    }

    return -1;
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

    uint32_t *resp = _maple_swap_data(0, 0, MAPLE_NAOMI_IO_REQUEST, sizeof(subcommand) / 4, subcommand);
    if(_maple_response_code(resp) != MAPLE_NAOMI_IO_RESPONSE)
    {
        return 0;
    }

    return 1;
}

/**
 * Request JVS button read from JVS ID addr and return buttons.
 *
 * Returns: 0 on success, and buttons is updated.
 *          -1 on failure to receive, or couldn't lock hardware.
 */
int maple_request_jvs_buttons(uint8_t addr, jvs_buttons_t *buttons)
{
    if (mutex_try_lock(&maple_mutex))
    {
        if (!__outstanding_request || __outstanding_request_addr != addr)
        {
            if(!__maple_request_jvs_send_buttons_packet(addr, 1))
            {
                // Didn't get a valid response for sending JVS.
                mutex_unlock(&maple_mutex);
                return -1;
            }
        }

        jvs_status_t status = _maple_request_recv_jvs();
        if(!status.packet_length)
        {
            // Didn't get a packet
            mutex_unlock(&maple_mutex);
            return -1;
        }

        if(!_jvs_packet_valid(status.packet))
        {
            // Packet failed CRC
            mutex_unlock(&maple_mutex);
            return -1;
        }

        if(_jvs_packet_code(status.packet) != 0x01)
        {
            // Packet is not response type.
            mutex_unlock(&maple_mutex);
            return -1;
        }
        if(_jvs_packet_payload(status.packet)[0] != 0x01)
        {
            // Packet is not report type.
            mutex_unlock(&maple_mutex);
            return -1;
        }

        // Parse out the buttons
        uint8_t *payload = _jvs_packet_payload(status.packet) + 1;
        buttons->dip1 = status.dip_switches & 0x1;
        buttons->dip2 = (status.dip_switches >> 1) & 0x1;
        buttons->dip3 = (status.dip_switches >> 2) & 0x1;
        buttons->dip4 = (status.dip_switches >> 3) & 0x1;
        buttons->psw1 = status.psw1;
        buttons->psw2 = status.psw2;
        buttons->test = (payload[0] >> 7) & 0x1;

        // Player 1 controls
        buttons->player1.service = (payload[1] >> 6) & 0x1;
        buttons->player1.start = (payload[1] >> 7) & 0x1;
        buttons->player1.up = (payload[1] >> 5) & 0x1;
        buttons->player1.down = (payload[1] >> 4) & 0x1;
        buttons->player1.left = (payload[1] >> 3) & 0x1;
        buttons->player1.right = (payload[1] >> 2) & 0x1;
        buttons->player1.button1 = (payload[1] >> 1) & 0x1;
        buttons->player1.button2 = payload[1] & 0x1;
        buttons->player1.button3 = (payload[2] >> 7) & 0x1;
        buttons->player1.button4 = (payload[2] >> 6) & 0x1;
        buttons->player1.button5 = (payload[2] >> 5) & 0x1;
        buttons->player1.button6 = (payload[2] >> 4) & 0x1;
        buttons->player1.analog1 = payload[11];
        buttons->player1.analog2 = payload[13];
        buttons->player1.analog3 = payload[15];
        buttons->player1.analog4 = payload[17];

        // Player 2 controls
        buttons->player2.service = (payload[3] >> 6) & 0x1;
        buttons->player2.start = (payload[3] >> 7) & 0x1;
        buttons->player2.up = (payload[3] >> 5) & 0x1;
        buttons->player2.down = (payload[3] >> 4) & 0x1;
        buttons->player2.left = (payload[3] >> 3) & 0x1;
        buttons->player2.right = (payload[3] >> 2) & 0x1;
        buttons->player2.button1 = (payload[3] >> 1) & 0x1;
        buttons->player2.button2 = payload[3] & 0x1;
        buttons->player2.button3 = (payload[4] >> 7) & 0x1;
        buttons->player2.button4 = (payload[4] >> 6) & 0x1;
        buttons->player2.button5 = (payload[4] >> 5) & 0x1;
        buttons->player2.button6 = (payload[4] >> 4) & 0x1;
        buttons->player2.analog1 = payload[19];
        buttons->player2.analog2 = payload[21];
        buttons->player2.analog3 = payload[23];
        buttons->player2.analog4 = payload[25];

        // Finally, send another request to be ready next time we poll.
        __outstanding_request = __maple_request_jvs_send_buttons_packet(addr, 1);
        __outstanding_request_addr = addr;
        mutex_unlock(&maple_mutex);
        return 0;
    }

    return -1;
}

int maple_poll_buttons()
{
    jvs_buttons_t new_buttons;
    if (maple_request_jvs_buttons(0x01, &new_buttons) == 0)
    {
        if (first_poll)
        {
            // We already polled at least once, so copy what we have to
            // the previous buttons.
            last_buttons = cur_buttons;
        }

        // Update the current buttons.
        cur_buttons = new_buttons;

        if (!first_poll)
        {
            // We haven't polled yet, set the last buttons to what we just
            // polled so if a user starts a game with a button held that is
            // doesn't register as pressed on the first loop. No half an A
            // press here!
            last_buttons = cur_buttons;
            first_poll = 1;
        }

        return 0;
    }

    return -1;
}

jvs_buttons_t maple_buttons_held()
{
    // Just return the state of all buttons currently.
    return cur_buttons;
}

uint8_t __press(uint8_t old, uint8_t new)
{
    return (old == 0 && new != 0) ? 1 : 0;
}

uint8_t __release(uint8_t old, uint8_t new)
{
    return (old != 0 && new == 0) ? 1 : 0;
}

jvs_buttons_t maple_buttons_pressed()
{
    jvs_buttons_t buttons;

    buttons.dip1 = __press(last_buttons.dip1, cur_buttons.dip1);
    buttons.dip2 = __press(last_buttons.dip2, cur_buttons.dip2);
    buttons.dip3 = __press(last_buttons.dip3, cur_buttons.dip3);
    buttons.dip4 = __press(last_buttons.dip4, cur_buttons.dip4);
    buttons.psw1 = __press(last_buttons.psw1, cur_buttons.psw1);
    buttons.psw2 = __press(last_buttons.psw2, cur_buttons.psw2);
    buttons.test = __press(last_buttons.test, cur_buttons.test);

    // Player 1 controls
    buttons.player1.service = __press(last_buttons.player1.service, cur_buttons.player1.service);
    buttons.player1.start = __press(last_buttons.player1.start, cur_buttons.player1.start);
    buttons.player1.up = __press(last_buttons.player1.up, cur_buttons.player1.up);
    buttons.player1.down = __press(last_buttons.player1.down, cur_buttons.player1.down);
    buttons.player1.left = __press(last_buttons.player1.left, cur_buttons.player1.left);
    buttons.player1.right = __press(last_buttons.player1.right, cur_buttons.player1.right);
    buttons.player1.button1 = __press(last_buttons.player1.button1, cur_buttons.player1.button1);
    buttons.player1.button2 = __press(last_buttons.player1.button2, cur_buttons.player1.button2);
    buttons.player1.button3 = __press(last_buttons.player1.button3, cur_buttons.player1.button3);
    buttons.player1.button4 = __press(last_buttons.player1.button4, cur_buttons.player1.button4);
    buttons.player1.button5 = __press(last_buttons.player1.button5, cur_buttons.player1.button5);
    buttons.player1.button6 = __press(last_buttons.player1.button6, cur_buttons.player1.button6);

    // Player 2 controls
    buttons.player2.service = __press(last_buttons.player2.service, cur_buttons.player2.service);
    buttons.player2.start = __press(last_buttons.player2.start, cur_buttons.player2.start);
    buttons.player2.up = __press(last_buttons.player2.up, cur_buttons.player2.up);
    buttons.player2.down = __press(last_buttons.player2.down, cur_buttons.player2.down);
    buttons.player2.left = __press(last_buttons.player2.left, cur_buttons.player2.left);
    buttons.player2.right = __press(last_buttons.player2.right, cur_buttons.player2.right);
    buttons.player2.button1 = __press(last_buttons.player2.button1, cur_buttons.player2.button1);
    buttons.player2.button2 = __press(last_buttons.player2.button2, cur_buttons.player2.button2);
    buttons.player2.button3 = __press(last_buttons.player2.button3, cur_buttons.player2.button3);
    buttons.player2.button4 = __press(last_buttons.player2.button4, cur_buttons.player2.button4);
    buttons.player2.button5 = __press(last_buttons.player2.button5, cur_buttons.player2.button5);
    buttons.player2.button6 = __press(last_buttons.player2.button6, cur_buttons.player2.button6);

    return buttons;
}

jvs_buttons_t maple_buttons_released()
{
    jvs_buttons_t buttons;

    buttons.dip1 = __release(last_buttons.dip1, cur_buttons.dip1);
    buttons.dip2 = __release(last_buttons.dip2, cur_buttons.dip2);
    buttons.dip3 = __release(last_buttons.dip3, cur_buttons.dip3);
    buttons.dip4 = __release(last_buttons.dip4, cur_buttons.dip4);
    buttons.psw1 = __release(last_buttons.psw1, cur_buttons.psw1);
    buttons.psw2 = __release(last_buttons.psw2, cur_buttons.psw2);
    buttons.test = __release(last_buttons.test, cur_buttons.test);

    // Player 1 controls
    buttons.player1.service = __release(last_buttons.player1.service, cur_buttons.player1.service);
    buttons.player1.start = __release(last_buttons.player1.start, cur_buttons.player1.start);
    buttons.player1.up = __release(last_buttons.player1.up, cur_buttons.player1.up);
    buttons.player1.down = __release(last_buttons.player1.down, cur_buttons.player1.down);
    buttons.player1.left = __release(last_buttons.player1.left, cur_buttons.player1.left);
    buttons.player1.right = __release(last_buttons.player1.right, cur_buttons.player1.right);
    buttons.player1.button1 = __release(last_buttons.player1.button1, cur_buttons.player1.button1);
    buttons.player1.button2 = __release(last_buttons.player1.button2, cur_buttons.player1.button2);
    buttons.player1.button3 = __release(last_buttons.player1.button3, cur_buttons.player1.button3);
    buttons.player1.button4 = __release(last_buttons.player1.button4, cur_buttons.player1.button4);
    buttons.player1.button5 = __release(last_buttons.player1.button5, cur_buttons.player1.button5);
    buttons.player1.button6 = __release(last_buttons.player1.button6, cur_buttons.player1.button6);

    // Player 2 controls
    buttons.player2.service = __release(last_buttons.player2.service, cur_buttons.player2.service);
    buttons.player2.start = __release(last_buttons.player2.start, cur_buttons.player2.start);
    buttons.player2.up = __release(last_buttons.player2.up, cur_buttons.player2.up);
    buttons.player2.down = __release(last_buttons.player2.down, cur_buttons.player2.down);
    buttons.player2.left = __release(last_buttons.player2.left, cur_buttons.player2.left);
    buttons.player2.right = __release(last_buttons.player2.right, cur_buttons.player2.right);
    buttons.player2.button1 = __release(last_buttons.player2.button1, cur_buttons.player2.button1);
    buttons.player2.button2 = __release(last_buttons.player2.button2, cur_buttons.player2.button2);
    buttons.player2.button3 = __release(last_buttons.player2.button3, cur_buttons.player2.button3);
    buttons.player2.button4 = __release(last_buttons.player2.button4, cur_buttons.player2.button4);
    buttons.player2.button5 = __release(last_buttons.player2.button5, cur_buttons.player2.button5);
    buttons.player2.button6 = __release(last_buttons.player2.button6, cur_buttons.player2.button6);

    return buttons;
}
