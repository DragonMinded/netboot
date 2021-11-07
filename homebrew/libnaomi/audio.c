#include <memory.h>
#include "naomi/audio.h"
#include "naomi/system.h"
#include "aica/common.h"

#define AICA_BASE 0xA0700000

#define AICA_VERSION (0x2800 >> 2)
#define AICA_RESET (0x2C00 >> 2)

// Our command buffer for talking to the AICA.
#define AICA_CMD_BUFFER(x) *((volatile uint32_t *)((SOUNDRAM_BASE | UNCACHED_MIRROR) + 0x20000 + x))

static int initialized = 0;

void try_fast_memcpy(void *dst, void *src, unsigned int length)
{
    unsigned int hw_copy_amount = length & 0xFFFFFFE0;
    if (hw_memcpy(dst, src, length))
    {
        for(unsigned int memloc = hw_copy_amount; memloc < length; memloc+= 4)
        {
            *((uint32_t *)(((uint32_t)dst) + memloc)) = *((uint32_t *)(((uint32_t)src) + memloc));
        }
    }
    else
    {
        // Failed to use HW memcpy, fall back to slow method.
        memcpy(dst, src, length);
    }

}

void load_aica_binary(void *binary, unsigned int length)
{
    // Note that for this to work without bugs or weirdness on the ARM side, the ARM
    // binary pointer should be 4-byte aligned, and the length should be a multiple
    // of 4. Any other configuration asks for undefined behavior.
    volatile unsigned int *aicabase = (volatile unsigned int *)AICA_BASE;

    // Set up 16-bit wave memory size.
    aicabase[AICA_VERSION] = 0x200;

    // Pull the AICA MCU in reset.
    aicabase[AICA_RESET] |= 0x1;

    // Copy the binary to the AICA MCU.
    try_fast_memcpy((void *)(SOUNDRAM_BASE | UNCACHED_MIRROR), binary, length);

    // Pull the AICA MCU back out of reset.
    aicabase[AICA_RESET] &= ~0x1;
}

uint32_t audio_aica_uptime()
{
    return AICA_CMD_BUFFER(CMD_BUFFER_UPTIME);
}

uint32_t __audio_exchange_command(uint32_t command, uint32_t *params)
{
    // Wait for the AICA to be ready for a command.
    while (AICA_CMD_BUFFER(CMD_BUFFER_BUSY) != 0) { ; }

    // Set up the command and registers.
    AICA_CMD_BUFFER(CMD_BUFFER_REQUEST) = command;

    if (params)
    {
        memcpy((void *)&AICA_CMD_BUFFER(CMD_BUFFER_PARAMS), params, sizeof(uint32_t) * 5);
    }

    // Trigger the AICA to react to the command.
    AICA_CMD_BUFFER(CMD_BUFFER_BUSY) = 1;

    // Wait for the AICA to finish the command.
    while (AICA_CMD_BUFFER(CMD_BUFFER_BUSY) != 0) { ; }

    // Return the response.
    return AICA_CMD_BUFFER(CMD_BUFFER_RESPONSE);
}

void audio_init()
{
    if (!initialized)
    {
        // Load up our binary so we can talk to it.
        load_aica_binary(AICA_DEFAULT_BINARY, AICA_DEFAULT_BINARY_SIZE);
        initialized = 1;
    }
}

void audio_free()
{
    if (initialized)
    {
        // Kill all running sounds.
        __audio_exchange_command(REQUEST_SILENCE, 0);
        initialized = 0;
    }
}

uint32_t __audio_get_location(int format, unsigned int samplerate, uint32_t speakers, unsigned int num_samples)
{
    uint32_t params[4] = {num_samples, format, samplerate, speakers};
    return __audio_exchange_command(REQUEST_ALLOCATE, params);
}

uint32_t __audio_load_location(int format, unsigned int samplerate, uint32_t speakers, void *data, unsigned int num_samples)
{
    uint32_t location = __audio_get_location(format, samplerate, speakers, num_samples);
    uint32_t size = (format == ALLOCATE_AUDIO_FORMAT_16BIT) ? (num_samples * 2) : num_samples;

    if (location != 0)
    {
        try_fast_memcpy((void *)((SOUNDRAM_BASE | UNCACHED_MIRROR) + location), data, size);
    }

    return location;
}

int audio_play_sound(int format, unsigned int samplerate, uint32_t speakers, void *data, unsigned int num_samples)
{
    uint32_t location = __audio_load_location(format, samplerate, speakers, data, num_samples);
    if (location)
    {
        uint32_t params[1] = {location};
        return __audio_exchange_command(REQUEST_START_PLAY, params) == RESPONSE_SUCCESS ? 0 : -1;
    }
    else
    {
        return -1;
    }

    // TODO: Need to free this sound once its done.
}
