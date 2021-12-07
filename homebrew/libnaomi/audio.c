#include <memory.h>
#include <math.h>
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

// FIFO wait register, so we don't overrun SH-4/AICA FIFO
#define AICA_FIFO_STATUS (*((volatile uint32_t *)0xA05F688C))

// Whether we've initialized this module or not.
static int initialized = 0;

// The location of the left and right channel of our ringbuffer.
static uint32_t leftchannel = 0;
static uint32_t rightchannel = 0;

// Accumulation mini-buffers since we can only write 32 bits at a time to a 32-bit
// aligned offset.
static uint32_t leftaccum = 0;
static uint32_t rightaccum = 0;
static int leftaccumamount = 0;
static int rightaccumamount = 0;

// Ringbuffer information, such as the format and size and current writing position.
static int ringformat = 0;
static int ringsize = 0;
static int ringleftpos = 0;
static int ringrightpos = 0;

// Safety value for leaving a chunk after the current playback pos alone.
#define RINGBUFFER_SAFETY_SIZE 512

void __aica_fifo_wait()
{
    while ((AICA_FIFO_STATUS & 0x11) != 0) { ; }
}

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
    uint32_t copyamount = 0;
    while (length > 0)
    {
        // Don't overrun the FIFO or we could get flaky transfers.
        if ((copyamount & 0x1F) == 0)
        {
            __aica_fifo_wait();
        }

        *dstptr = *srcptr;
        dstptr++;
        srcptr++;
        length-=4;
        copyamount+=4;
    }

    // Make sure we exit only when the FIFO is done.
    __aica_fifo_wait();
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
    // Make sure we have room in the FIFO to read the uptime.
    __aica_fifo_wait();

    // Return the uptime in milliseconds as written by the AICA binary.
    return AICA_CMD_BUFFER(CMD_BUFFER_UPTIME);
}

uint32_t __audio_exchange_command(uint32_t command, uint32_t param1, uint32_t param2, uint32_t param3, uint32_t param4)
{
    // Make sure we have room in the FIFO to read busy register.
    __aica_fifo_wait();

    // Wait for the AICA to be ready for a command.
    while (AICA_CMD_BUFFER(CMD_BUFFER_BUSY) != 0) { ; }

    // Make sure we have room in the FIFO to write the command.
    __aica_fifo_wait();

    // Set up the command and param registers.
    AICA_CMD_BUFFER(CMD_BUFFER_REQUEST) = command;
    AICA_CMD_BUFFER(CMD_BUFFER_PARAMS + 0x0) = param1;
    AICA_CMD_BUFFER(CMD_BUFFER_PARAMS + 0x4) = param2;
    AICA_CMD_BUFFER(CMD_BUFFER_PARAMS + 0x8) = param3;
    AICA_CMD_BUFFER(CMD_BUFFER_PARAMS + 0xC) = param4;

    // Trigger the AICA to react to the command.
    AICA_CMD_BUFFER(CMD_BUFFER_BUSY) = 1;

    // Make sure we have room in the FIFO to wait for done.
    __aica_fifo_wait();

    // Wait for the AICA to finish the command.
    while (AICA_CMD_BUFFER(CMD_BUFFER_BUSY) != 0) { ; }

    // Make sure we have room in the FIFO to read the response.
    __aica_fifo_wait();

    // Return the response.
    return AICA_CMD_BUFFER(CMD_BUFFER_RESPONSE);
}

void audio_init()
{
    if (!initialized)
    {
        // Load up our binary so we can talk to it.
        load_aica_binary(AICA_DEFAULT_BINARY, AICA_DEFAULT_BINARY_SIZE);
        leftchannel = 0;
        rightchannel = 0;
        ringformat = 0;
        ringsize = 0;
        ringleftpos = 0;
        ringrightpos = 0;
        leftaccum = 0;
        rightaccum = 0;
        leftaccumamount = 0;
        rightaccumamount = 0;
        initialized = 1;
    }
}

void audio_free()
{
    if (initialized)
    {
        // Kill all running sounds.
        __audio_exchange_command(REQUEST_SILENCE, 0, 0, 0, 0);
        leftchannel = 0;
        rightchannel = 0;
        ringformat = 0;
        ringsize = 0;
        ringleftpos = 0;
        ringrightpos = 0;
        leftaccum = 0;
        rightaccum = 0;
        leftaccumamount = 0;
        rightaccumamount = 0;
        initialized = 0;
    }
}

unsigned int __audio_volume_to_loudness(float volume)
{
    if (volume >= 1.0) { return 255; }
    if (volume <= 0.0) { return 0; }

    unsigned int loudness = (int)(pow(10.0, log10(volume) / 2.0) * 255.0);
    if (loudness > 255) { loudness = 255; }
    return loudness;
}

uint32_t __audio_get_location(int format, unsigned int samplerate, unsigned int num_samples)
{
    return __audio_exchange_command(
        REQUEST_ALLOCATE,
        num_samples,
        format == AUDIO_FORMAT_16BIT ? ALLOCATE_AUDIO_FORMAT_16BIT : ALLOCATE_AUDIO_FORMAT_8BIT,
        samplerate,
        0
    );
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

int audio_play_sound(int format, unsigned int samplerate, uint32_t speakers, float volume, void *data, unsigned int num_samples)
{
    uint32_t location = __audio_load_location(format, samplerate, data, num_samples);
    if (location != 0)
    {
        uint32_t panning = 0;
        if (speakers & SPEAKER_LEFT)
        {
            panning |= ALLOCATE_SPEAKER_LEFT;
        }
        if (speakers & SPEAKER_RIGHT)
        {
            panning |= ALLOCATE_SPEAKER_RIGHT;
        }
        uint32_t loudness = __audio_volume_to_loudness(volume);

        if (__audio_exchange_command(REQUEST_DISCARD_AFTER_USE, location, 0, 0, 0) != RESPONSE_SUCCESS)
        {
            return -1;
        }

        // Now, play the track.
        return __audio_exchange_command(REQUEST_START_PLAY, location, panning, loudness, 0) == RESPONSE_SUCCESS ? 0 : -1;
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
        __audio_exchange_command(REQUEST_FREE, sound, 0, 0, 0);
    }
}

int audio_set_registered_sound_loop(int sound, unsigned int loop_point)
{
    if (sound > 0)
    {
        return __audio_exchange_command(REQUEST_SET_LOOP_POINT, sound, loop_point, 0, 0);
    }
    else
    {
        return -1;
    }
}

int audio_clear_registered_sound_loop(int sound)
{
    if (sound > 0)
    {
        return __audio_exchange_command(REQUEST_CLEAR_LOOP_POINT, sound, 0, 0, 0);
    }
    else
    {
        return -1;
    }
}

int audio_play_registered_sound(int sound, uint32_t speakers, float volume)
{
    if (sound > 0)
    {
        uint32_t panning = 0;
        if (speakers & SPEAKER_LEFT)
        {
            panning |= ALLOCATE_SPEAKER_LEFT;
        }
        if (speakers & SPEAKER_RIGHT)
        {
            panning |= ALLOCATE_SPEAKER_RIGHT;
        }
        uint32_t loudness = __audio_volume_to_loudness(volume);

        return __audio_exchange_command(REQUEST_START_PLAY, sound, panning, loudness, 0) == RESPONSE_SUCCESS ? 0 : -1;
    }
    else
    {
        return -1;
    }
}

int audio_stop_registered_sound(int sound)
{
    if (sound > 0)
    {
        return __audio_exchange_command(REQUEST_STOP_PLAY, sound, 0, 0, 0);
    }
    else
    {
        return -1;
    }
}

void audio_unregister_ringbuffer()
{
    if (leftchannel || rightchannel)
    {
        __audio_exchange_command(REQUEST_STOP_STEREO_RINGBUFFER, 0, 0, 0, 0);
        leftchannel = 0;
        rightchannel = 0;
        ringformat = 0;
        ringsize = 0;
        ringleftpos = 0;
        ringrightpos = 0;
        leftaccum = 0;
        rightaccum = 0;
        leftaccumamount = 0;
        rightaccumamount = 0;
    }
}

int audio_register_ringbuffer(int format, unsigned int samplerate, unsigned int num_samples)
{
    // If there is already a ringbuffer, request to free it.
    if (leftchannel || rightchannel)
    {
        audio_unregister_ringbuffer();
    }

    if (num_samples <= RINGBUFFER_SAFETY_SIZE)
    {
        // Not enough room in the ringbuffer to do anything!
        return -1;
    }

    // Make sure we can't end up with an invalid ringbuffer size.
    if (format == AUDIO_FORMAT_16BIT)
    {
        if (num_samples & 0x1)
        {
            return -1;
        }
    }
    else
    {
        if (num_samples & 0x3)
        {
            return -1;
        }
    }

    // Now request a new stereo ringbuffer.
    int retval = __audio_exchange_command(
        REQUEST_START_STEREO_RINGBUFFER,
        num_samples,
        format == AUDIO_FORMAT_16BIT ? ALLOCATE_AUDIO_FORMAT_16BIT : ALLOCATE_AUDIO_FORMAT_8BIT,
        samplerate,
        0
    );

    if (retval == RESPONSE_FAILURE)
    {
        return -1;
    }

    // Now, grab the location of the two buffers.
    leftchannel = __audio_exchange_command(REQUEST_RINGBUFFER_LOCATION, CHANNEL_LEFT, 0, 0, 0);
    rightchannel = __audio_exchange_command(REQUEST_RINGBUFFER_LOCATION, CHANNEL_RIGHT, 0, 0, 0);
    if (leftchannel == 0 || rightchannel == 0)
    {
        __audio_exchange_command(REQUEST_STOP_STEREO_RINGBUFFER, 0, 0, 0, 0);
        return -1;
    }

    // Set up parameters for ringbuffer writing.
    ringformat = format;
    ringsize = num_samples;
    ringleftpos = RINGBUFFER_SAFETY_SIZE;
    ringrightpos = RINGBUFFER_SAFETY_SIZE;

    // Set up mini accumulation buffers.
    leftaccum = 0;
    rightaccum = 0;
    leftaccumamount = 0;
    rightaccumamount = 0;

    // Success!
    return 0;
}

int audio_write_stereo_data(void *data, unsigned int num_samples)
{
    if (!leftchannel || !rightchannel)
    {
        // We're not initialized!
        return -1;
    }

    // First, we need to get the current playback position for this channel.
    int leftreadposition = 0;
    int rightreadposition = 0;
    int leftwriteposition = 0;
    int rightwriteposition = 0;

    void *leftringbuffer = 0;
    void *rightringbuffer = 0;

    leftreadposition = __audio_exchange_command(REQUEST_RINGBUFFER_POSITION, CHANNEL_LEFT, 0, 0, 0);
    leftwriteposition = ringleftpos;
    leftringbuffer = (void *)((SOUNDRAM_BASE | UNCACHED_MIRROR) + leftchannel);

    rightreadposition = __audio_exchange_command(REQUEST_RINGBUFFER_POSITION, CHANNEL_RIGHT, 0, 0, 0);
    rightwriteposition = ringrightpos;
    rightringbuffer = (void *)((SOUNDRAM_BASE | UNCACHED_MIRROR) + rightchannel);

    // Now, if the position is less than the write location, that means its wrapepd
    // around, so we need to account for that.
    int leftavailable = 0;
    if (leftreadposition < leftwriteposition)
    {
        leftavailable = (leftreadposition + ringsize) - leftwriteposition;
    }
    else
    {
        leftavailable = leftreadposition - leftwriteposition;
    }

    int rightavailable = 0;
    if (rightreadposition < rightwriteposition)
    {
        rightavailable = (rightreadposition + ringsize) - rightwriteposition;
    }
    else
    {
        rightavailable = rightreadposition - rightwriteposition;
    }
    leftavailable -= RINGBUFFER_SAFETY_SIZE;
    rightavailable -= RINGBUFFER_SAFETY_SIZE;

    // Start by waiting for the FIFO to be ready.
    int byteswritten = 0;
    __aica_fifo_wait();

    // Calculate how many actual samples we can copy over, since everything
    // must be 32-bit aligned.
    unsigned int actual_samples = num_samples;
    if (leftavailable < actual_samples) { actual_samples = leftavailable; }
    if (rightavailable < actual_samples) { actual_samples = rightavailable; }

    unsigned int written = 0;
    while (written < actual_samples)
    {
        // Don't overrun the FIFO or we could get flaky transfers.
        if ((byteswritten > 0x1F))
        {
            __aica_fifo_wait();
            byteswritten = 0;
        }

        if (ringformat == AUDIO_FORMAT_16BIT)
        {
            uint16_t *copybuf = (uint16_t *)data;
            ((uint16_t *)&leftaccum)[leftaccumamount] = copybuf[written * 2];
            leftaccumamount++;
            written++;

            if (leftaccumamount == 2)
            {
                uint32_t *leftringpos = (uint32_t *)(((uint16_t *)leftringbuffer) + leftwriteposition);
                if (((uint32_t)leftringpos & 0x3) != 0)
                {
                    _irq_display_invariant("ringbuffer failure", "somehow got a non-aligned ringbuffer write offset!");
                }

                *leftringpos = leftaccum;
                leftaccumamount = 0;
                leftwriteposition = (leftwriteposition + 2) % ringsize;
                byteswritten += 4;
            }
        }
        else if (ringformat == AUDIO_FORMAT_8BIT)
        {
            uint8_t *copybuf = (uint8_t *)data;
            ((uint8_t *)&leftaccum)[leftaccumamount] = copybuf[written * 2];
            leftaccumamount++;
            written++;

            if (leftaccumamount == 4)
            {
                uint32_t *leftringpos = (uint32_t *)(((uint8_t *)leftringbuffer) + leftwriteposition);
                if (((uint32_t)leftringpos & 0x3) != 0)
                {
                    _irq_display_invariant("ringbuffer failure", "somehow got a non-aligned ringbuffer write offset!");
                }

                *leftringpos = leftaccum;
                leftaccumamount = 0;
                leftwriteposition = (leftwriteposition + 4) % ringsize;
                byteswritten += 4;
            }
        }
        else
        {
            _irq_display_invariant("audio failure", "unrecognized ringbuffer format %d", ringformat);
        }
    }

    written = 0;
    while (written < actual_samples)
    {
        // Don't overrun the FIFO or we could get flaky transfers.
        if ((byteswritten > 0x1F))
        {
            __aica_fifo_wait();
            byteswritten = 0;
        }

        if (ringformat == AUDIO_FORMAT_16BIT)
        {
            uint16_t *copybuf = (uint16_t *)data;
            ((uint16_t *)&rightaccum)[rightaccumamount] = copybuf[(written * 2) + 1];
            rightaccumamount++;
            written++;

            if (rightaccumamount == 2)
            {
                uint32_t *rightringpos = (uint32_t *)(((uint16_t *)rightringbuffer) + rightwriteposition);
                if (((uint32_t)rightringpos & 0x3) != 0)
                {
                    _irq_display_invariant("ringbuffer failure", "somehow got a non-aligned ringbuffer write offset!");
                }

                *rightringpos = rightaccum;
                rightaccumamount = 0;
                rightwriteposition = (rightwriteposition + 2) % ringsize;
                byteswritten += 4;
            }
        }
        else if (ringformat == AUDIO_FORMAT_8BIT)
        {
            uint8_t *copybuf = (uint8_t *)data;
            ((uint8_t *)&rightaccum)[rightaccumamount] = copybuf[(written * 2) + 1];
            rightaccumamount++;
            written++;

            if (rightaccumamount == 4)
            {
                uint32_t *rightringpos = (uint32_t *)(((uint8_t *)rightringbuffer) + rightwriteposition);
                if (((uint32_t)rightringpos & 0x3) != 0)
                {
                    _irq_display_invariant("ringbuffer failure", "somehow got a non-aligned ringbuffer write offset!");
                }

                *rightringpos = rightaccum;
                rightaccumamount = 0;
                rightwriteposition = (rightwriteposition + 4) % ringsize;
                byteswritten += 4;
            }
        }
        else
        {
            _irq_display_invariant("audio failure", "unrecognized ringbuffer format %d", ringformat);
        }

        byteswritten += 4;
    }

    ringleftpos = leftwriteposition;
    ringrightpos = rightwriteposition;

    return actual_samples;
}

int audio_write_mono_data(int channel, void *data, unsigned int num_samples)
{
    if (!leftchannel || !rightchannel)
    {
        // We're not initialized!
        return -1;
    }

    // First, we need to get the current playback position for this channel.
    int readposition = 0;
    int writeposition = 0;
    uint32_t accum = 0;
    int accumamount = 0;
    void *ringbuffer = 0;
    if (channel == AUDIO_CHANNEL_LEFT)
    {
        readposition = __audio_exchange_command(REQUEST_RINGBUFFER_POSITION, CHANNEL_LEFT, 0, 0, 0);
        writeposition = ringleftpos;
        ringbuffer = (void *)((SOUNDRAM_BASE | UNCACHED_MIRROR) + leftchannel);
        accum = leftaccum;
        accumamount = leftaccumamount;
    }
    else if (channel == AUDIO_CHANNEL_RIGHT)
    {
        readposition = __audio_exchange_command(REQUEST_RINGBUFFER_POSITION, CHANNEL_RIGHT, 0, 0, 0);
        writeposition = ringrightpos;
        ringbuffer = (void *)((SOUNDRAM_BASE | UNCACHED_MIRROR) + rightchannel);
        accum = rightaccum;
        accumamount = rightaccumamount;
    }
    else
    {
        // This isn't a valid channel.
        return -1;
    }

    // Now, if the position is less than the write location, that means its wrapepd
    // around, so we need to account for that.
    int available = 0;
    if (readposition < writeposition)
    {
        available = (readposition + ringsize) - writeposition;
    }
    else
    {
        available = readposition - writeposition;
    }
    available -= RINGBUFFER_SAFETY_SIZE;

    // Start by waiting for the FIFO to be ready.
    int byteswritten = 0;
    __aica_fifo_wait();

    // Calculate how many actual samples we can copy over, since everything
    // must be 32-bit aligned.
    unsigned int actual_samples = num_samples;
    if (available < actual_samples) { actual_samples = available; }

    unsigned int written = 0;
    while (written < actual_samples)
    {
        // Don't overrun the FIFO or we could get flaky transfers.
        if ((byteswritten > 0x1F))
        {
            __aica_fifo_wait();
            byteswritten = 0;
        }

        if (ringformat == AUDIO_FORMAT_16BIT)
        {
            uint16_t *copybuf = (uint16_t *)data;
            ((uint16_t *)&accum)[accumamount] = copybuf[written];
            accumamount++;
            written++;

            if (accumamount == 2)
            {
                uint32_t *ringpos = (uint32_t *)(((uint16_t *)ringbuffer) + writeposition);
                if (((uint32_t)ringpos & 0x3) != 0)
                {
                    _irq_display_invariant("ringbuffer failure", "somehow got a non-aligned ringbuffer write offset!");
                }

                *ringpos = accum;
                accumamount = 0;
                writeposition = (writeposition + 2) % ringsize;
                byteswritten += 4;
            }
        }
        else if (ringformat == AUDIO_FORMAT_8BIT)
        {
            uint8_t *copybuf = (uint8_t *)data;
            ((uint8_t *)&accum)[accumamount] = copybuf[written];
            accumamount++;
            written++;

            if (accumamount == 4)
            {
                uint32_t *ringpos = (uint32_t *)(((uint8_t *)ringbuffer) + writeposition);
                if (((uint32_t)ringpos & 0x3) != 0)
                {
                    _irq_display_invariant("ringbuffer failure", "somehow got a non-aligned ringbuffer write offset!");
                }

                *ringpos = accum;
                accumamount = 0;
                writeposition = (writeposition + 4) % ringsize;
                byteswritten += 4;
            }
        }
        else
        {
            _irq_display_invariant("audio failure", "unrecognized ringbuffer format %d", ringformat);
        }
    }

    if (channel == AUDIO_CHANNEL_LEFT)
    {
        ringleftpos = writeposition;
        leftaccum = accum;
        leftaccumamount = accumamount;
    }
    else if (channel == AUDIO_CHANNEL_RIGHT)
    {
        ringrightpos = writeposition;
        rightaccum = accum;
        rightaccumamount = accumamount;
    }

    return actual_samples;
}
