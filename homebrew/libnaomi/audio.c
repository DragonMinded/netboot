#include <memory.h>
#include "naomi/audio.h"
#include "naomi/system.h"
#include "aica/common.h"
#include "irqinternal.h"

// Base registers for controlling the AICA.
#define AICA_BASE 0xA0700000

// Register offset definitions.
#define AICA_VERSION (0x2800 >> 2)
#define AICA_RESET (0x2C00 >> 2)

// Our command buffer for talking to the AICA.
#define AICA_CMD_BUFFER(x) *((volatile uint32_t *)((SOUNDRAM_BASE | UNCACHED_MIRROR) + 0x20000 + x))

// Whether we've initialized this module or not.
static int initialized = 0;

void __aica_memcpy(void *dst, void *src, unsigned int length)
{
    if ((((uint32_t)dst) & 0x3) != 0)
    {
        _irq_display_invariant("invalid memcpy location", "dst %08lx is not aligned to 4-byte boundary", (uint32_t)dst);
    }
    if ((((uint32_t)src) & 0x3) != 0)
    {
        _irq_display_invariant("invalid memcpy location", "src %08lx is not aligned to 4-byte boundary", (uint32_t)src);
    }

    // Round up to next 4-byte boundary.
    length = (length + 3) & 0xFFFFFFFC;

    uint32_t *dstptr = (uint32_t *)dst;
    uint32_t *srcptr = (uint32_t *)src;
    while (length > 0)
    {
        *dstptr = *srcptr;
        dstptr++;
        srcptr++;
        length-=4;
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

    // Copy the binary to the AICA MCU. Its safe to do this here since the AICA
    // is in reset, so there will be no G1 FIFO contention.
    __aica_memcpy((void *)(SOUNDRAM_BASE | UNCACHED_MIRROR), binary, length);

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
        for (int param = 0; param < 4; param ++)
        {
            AICA_CMD_BUFFER(CMD_BUFFER_PARAMS + (param * 4)) = params[param];
        }
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

uint32_t __audio_get_location(int format, unsigned int samplerate, unsigned int num_samples)
{
    uint32_t params[4] = {
        num_samples,
        format == AUDIO_FORMAT_16BIT ? ALLOCATE_AUDIO_FORMAT_16BIT : ALLOCATE_AUDIO_FORMAT_8BIT,
        samplerate,
    };
    return __audio_exchange_command(REQUEST_ALLOCATE, params);
}

uint32_t __audio_load_location(int format, unsigned int samplerate, void *data, unsigned int num_samples)
{
    uint32_t location = __audio_get_location(format, samplerate, num_samples);
    uint32_t size = (format == AUDIO_FORMAT_16BIT) ? (num_samples * 2) : num_samples;

    if (location != 0)
    {
        __aica_memcpy((void *)((SOUNDRAM_BASE | UNCACHED_MIRROR) + location), data, size);
    }

    return location;
}

int audio_play_sound(int format, unsigned int samplerate, uint32_t speakers, void *data, unsigned int num_samples)
{
    uint32_t location = __audio_load_location(format, samplerate, data, num_samples);
    if (location != 0)
    {
        uint32_t params[4] = {location, 0};
        if (speakers & SPEAKER_LEFT)
        {
            params[1] |= ALLOCATE_SPEAKER_LEFT;
        }
        if (speakers & SPEAKER_RIGHT)
        {
            params[1] |= ALLOCATE_SPEAKER_RIGHT;
        }

        // Technically this only takes one parameter, but whatever.
        if (__audio_exchange_command(REQUEST_DISCARD_AFTER_USE, params) != RESPONSE_SUCCESS)
        {
            return -1;
        }

        // Now, play the track.
        return __audio_exchange_command(REQUEST_START_PLAY, params) == RESPONSE_SUCCESS ? 0 : -1;
    }
    else
    {
        return -1;
    }
}

int audio_register_sound(int format, unsigned int samplerate, void *data, unsigned int num_samples)
{
    uint32_t location = __audio_load_location(format, samplerate, data, num_samples);

    if (location)
    {
        return location;
    }
    else
    {
        return -1;
    }
}

void audio_unregister_sound(int sound)
{
    if (sound > 0)
    {
        uint32_t params[4] = {sound};
        __audio_exchange_command(REQUEST_FREE, params);
    }
}

int audio_play_registered_sound(int sound, uint32_t speakers)
{
    if (sound > 0)
    {
        uint32_t params[4] = {sound, 0};
        if (speakers & SPEAKER_LEFT)
        {
            params[1] |= ALLOCATE_SPEAKER_LEFT;
        }
        if (speakers & SPEAKER_RIGHT)
        {
            params[1] |= ALLOCATE_SPEAKER_RIGHT;
        }

        return __audio_exchange_command(REQUEST_START_PLAY, params) == RESPONSE_SUCCESS ? 0 : -1;
    }
    else
    {
        return -1;
    }
}
