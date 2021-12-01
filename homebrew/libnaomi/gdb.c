#include <string.h>
#include <stdint.h>
#include "naomi/cart.h"

#define MAX_PACKET_SIZE 512

static uint32_t response_address = 0;

int _gdb_has_response()
{
    return response_address != 0;
}

int _gdb_check_address(uint32_t address)
{
    uint8_t crc = ~(((address & 0xFF) + ((address >> 8) & 0xFF) + ((address >> 16) & 0xFF)) & 0xFF);
    return ((address >> 24) & 0xFF) == (crc & 0xFF);
}

uint32_t _gdb_make_address()
{
    uint32_t crc = ~(((response_address & 0xFF) + ((response_address >> 8) & 0xFF) + ((response_address >> 16) & 0xFF)) & 0xFF);
    uint32_t response = ((crc << 24) & 0xFF000000) | (response_address & 0x00FFFFFF);
    response_address = 0;
    return response;
}

void _gdb_make_valid_response(uint32_t address, char *response)
{
    // Remember where we put this response.
    response_address = address;

    // Write out that it's valid.
    uint32_t valid = 0xFFFFFFFF;
    cart_write(address, &valid, 4);

    // Write out how big it is.
    uint32_t size = strlen(response);
    cart_write(address + 4, &size, 4);

    if (size > 0)
    {
        // Write out the actual data
        cart_write(address + 8, response, size);
    }
}

void _gdb_make_invalid_response(uint32_t address)
{
    // Remember where we put this response.
    response_address = address;

    // Write out that it's invalid.
    uint32_t valid = 0x0;
    cart_write(address, &valid, 4);

    // Write out how big it is.
    uint32_t size = 0;
    cart_write(address + 4, &size, 4);
}

void _gdb_handle_command(uint32_t address)
{
    // TODO: Handle commands!

    // Unrecognized packet, so send a negative response.
    _gdb_make_invalid_response(address + MAX_PACKET_SIZE);
}
