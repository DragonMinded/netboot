#include <stdint.h>
#include "clib.h"
#include "common.h"

// Some system constants
#define FORMAT_16BIT 0
#define FORMAT_8BIT  1
#define FORMAT_ADPCM 3

#define VOL_MAX    0x00
#define VOL_MIN    0xff

#define PAN_LEFT   0x1f
#define PAN_RIGHT  0x0f
#define PAN_CENTER 0x00

// Base register location for all AICA registers
#define AICA_BASE 0x800000

// Base channel offset calculator, to be used with below
// register constants.
#define CHANNEL(channel, reg) (((channel) << 7) + (reg))
#define AICA_CFG_ADDR_HIGH (0x00 >> 2)
#define AICA_CFG_ADDR_LOW (0x04 >> 2)
#define AICA_CFG_LOOP_START (0x08 >> 2)
#define AICA_CFG_LOOP_END (0x0C >> 2)
#define AICA_CFG_ADSR1 (0x10 >> 2)
#define AICA_CFG_ADSR2 (0x14 >> 2)
#define AICA_CFG_PITCH (0x18 >> 2)
#define AICA_CFG_LFO1 (0x1C >> 2)
#define AICA_CFG_LFO2 (0x20 >> 2)
#define AICA_CFG_PAN_VOLUME (0x24 >> 2)
#define AICA_CFG_VOLUME2 (0x28 >> 2)
#define AICA_CFG_UNKNOWN1 (0x2C >> 2)
#define AICA_CFG_UNKNOWN2 (0x30 >> 2)
#define AICA_CFG_UNKNOWN3 (0x34 >> 2)
#define AICA_CFG_UNKNOWN4 (0x38 >> 2)
#define AICA_CFG_UNKNOWN5 (0x3C >> 2)
#define AICA_CFG_UNKNOWN6 (0x40 >> 2)
#define AICA_CFG_UNKNOWN7 (0x44 >> 2)

// Common registers
#define AICA_VERSION (0x2800 >> 2)

// Our command buffer for talking to the SH.
#define AICA_CMD_BUFFER(x) *((volatile uint32_t *)(0x20000 + x))

// The maximum hardware supported channels.
#define AICA_MAX_CHANNELS 64

// Our sample locations.
#define FIRST_SAMPLE_LOCATION 0x20100

// Millisecond timer, defined in arm-crt0.s
extern uint32_t millisecond_timer;

// Generated pitch table function in pitchtable.c
uint32_t pitch_reg(unsigned int samplerate);

void aica_reset()
{
    volatile uint32_t *aicabase = (volatile uint32_t *)AICA_BASE;

    // Set master DAC volume to 0 when initializing registers.
    aicabase[AICA_VERSION] = aicabase[AICA_VERSION] & 0xFFFFFFF0;

    /* Reset all 64 channels to a silent state */
    for(unsigned int chan = 0; chan < AICA_MAX_CHANNELS; chan++)
    {
        // Initialize important registers
        aicabase[CHANNEL(chan, AICA_CFG_ADDR_HIGH)] = 0x8000;
        aicabase[CHANNEL(chan, AICA_CFG_ADDR_LOW)] = 0;
        aicabase[CHANNEL(chan, AICA_CFG_LOOP_START)] = 0;
        aicabase[CHANNEL(chan, AICA_CFG_LOOP_END)] = 0;
        aicabase[CHANNEL(chan, AICA_CFG_ADSR1)] = 0;
        aicabase[CHANNEL(chan, AICA_CFG_ADSR2)] = 0;
        aicabase[CHANNEL(chan, AICA_CFG_PITCH)] = 0;
        aicabase[CHANNEL(chan, AICA_CFG_LFO1)] = 0;
        aicabase[CHANNEL(chan, AICA_CFG_LFO2)] = 0;
        aicabase[CHANNEL(chan, AICA_CFG_PAN_VOLUME)] = 0;
        aicabase[CHANNEL(chan, AICA_CFG_VOLUME2)] = 0xFF04;
        aicabase[CHANNEL(chan, AICA_CFG_UNKNOWN1)] = 0x1F77;
        aicabase[CHANNEL(chan, AICA_CFG_UNKNOWN2)] = 0x1F77;
        aicabase[CHANNEL(chan, AICA_CFG_UNKNOWN3)] = 0x1F77;
        aicabase[CHANNEL(chan, AICA_CFG_UNKNOWN4)] = 0x1F77;
        aicabase[CHANNEL(chan, AICA_CFG_UNKNOWN5)] = 0x1F77;
        aicabase[CHANNEL(chan, AICA_CFG_UNKNOWN6)] = 0;
        aicabase[CHANNEL(chan, AICA_CFG_UNKNOWN7)] = 0;
    }

    // Set master DAC volume back to full volume
    aicabase[AICA_VERSION] = (aicabase[AICA_VERSION] & 0xFFFFFFF0) | 0xF;
}

void aica_start_sound_oneshot(int channel, void *data, int format, int num_samples, int sample_rate, int vol, int pan)
{
    if (num_samples <= 0)
    {
        // Nothing to play?
        return;
    }
    if (sample_rate < 1000)
    {
        sample_rate = 1000;
    }
    if (sample_rate > 96000)
    {
        sample_rate = 96000;
    }

    volatile uint32_t *aicabase = (volatile uint32_t *)AICA_BASE;

    /* Set sample format and buffer address */
    aicabase[CHANNEL(channel, AICA_CFG_ADDR_HIGH)] = 0x8000 | ((format & 0x3) << 7) | ((((unsigned long)data) >> 16) & 0x7F);
    aicabase[CHANNEL(channel, AICA_CFG_ADDR_LOW)] = ((unsigned long)data) & 0xFFFF;

    /* Number of samples */
    aicabase[CHANNEL(channel, AICA_CFG_LOOP_START)] = 0;
    aicabase[CHANNEL(channel, AICA_CFG_LOOP_END)] = num_samples;

    /* Convert samplerate to pitch register format */
    aicabase[CHANNEL(channel, AICA_CFG_PITCH)] = pitch_reg(sample_rate);

    /* Set volume, pan, and some other stuff */
    aicabase[CHANNEL(channel, AICA_CFG_PAN_VOLUME)] = (pan & 0x1F) | (0xD << 8);
    aicabase[CHANNEL(channel, AICA_CFG_VOLUME2)] = 0x20 | ((vol & 0xFF) << 8);
    aicabase[CHANNEL(channel, AICA_CFG_ADSR1)] = 0x001F;
    aicabase[CHANNEL(channel, AICA_CFG_ADSR2)] = 0x001F;
    aicabase[CHANNEL(channel, AICA_CFG_LFO1)] = 0;
    aicabase[CHANNEL(channel, AICA_CFG_LFO2)] = 0;

    /* Enable playback */
    aicabase[CHANNEL(channel, AICA_CFG_ADDR_HIGH)] = (aicabase[CHANNEL(channel, AICA_CFG_ADDR_HIGH)] & 0x3FFF) | 0xC000;
}

void aica_start_sound_loop(int channel, void *data, int format, int num_samples, int sample_rate, int vol, int pan, int loop_restart_position)
{
    if (num_samples <= 0)
    {
        // Nothing to play?
        return;
    }
    if (loop_restart_position < 0)
    {
        loop_restart_position = 0;
    }
    if (loop_restart_position > num_samples)
    {
        loop_restart_position = num_samples;
    }
    if (sample_rate < 1000)
    {
        sample_rate = 1000;
    }
    if (sample_rate > 96000)
    {
        sample_rate = 96000;
    }

    volatile uint32_t *aicabase = (volatile uint32_t *)AICA_BASE;

    /* Set sample format and buffer address */
    aicabase[CHANNEL(channel, AICA_CFG_ADDR_HIGH)] = 0x8200 | ((format & 0x3) << 7) | ((((unsigned long)data) >> 16) & 0x7F);
    aicabase[CHANNEL(channel, AICA_CFG_ADDR_LOW)] = ((unsigned long)data) & 0xFFFF;

    /* Number of samples */
    aicabase[CHANNEL(channel, AICA_CFG_LOOP_START)] = loop_restart_position;
    aicabase[CHANNEL(channel, AICA_CFG_LOOP_END)] = num_samples;

    /* Convert samplerate to pitch register format */
    aicabase[CHANNEL(channel, AICA_CFG_PITCH)] = pitch_reg(sample_rate);

    /* Set volume, pan, and some other stuff */
    aicabase[CHANNEL(channel, AICA_CFG_PAN_VOLUME)] = (pan & 0x1F) | (0xD << 8);
    aicabase[CHANNEL(channel, AICA_CFG_VOLUME2)] = 0x20 | ((vol & 0xFF) << 8);
    aicabase[CHANNEL(channel, AICA_CFG_ADSR1)] = 0x001F;
    aicabase[CHANNEL(channel, AICA_CFG_ADSR2)] = 0x001F;
    aicabase[CHANNEL(channel, AICA_CFG_LFO1)] = 0;
    aicabase[CHANNEL(channel, AICA_CFG_LFO2)] = 0;

    /* Enable playback */
    aicabase[CHANNEL(channel, AICA_CFG_ADDR_HIGH)] = (aicabase[CHANNEL(channel, AICA_CFG_ADDR_HIGH)] & 0x3FFF) | 0xC000;
}

void aica_stop_sound(int channel)
{
    volatile uint32_t *aicabase = (volatile uint32_t *)AICA_BASE;

    // Don't forget to clear not just the start bit, but also the loop bit as well.
    aicabase[CHANNEL(channel, AICA_CFG_ADDR_HIGH)] = (aicabase[CHANNEL(channel, AICA_CFG_ADDR_HIGH)] & 0x3DFF) | 0x8000;
}

#define FLAGS_IN_USE 0x1
#define FLAGS_DISCARD_AFTER_USE 0x2
#define FLAGS_LOOP 0x4

typedef struct sample_info_struct
{
    // Flags, such as if this sample is in use or not, if it should be discarded
    // after playing, whether it is a looping sample or regular sample, etc.
    // Whether this registered sample is in use or not.
    uint32_t flags;
    // The raw location in memory this sample resides.
    uint32_t location;
    // The raw size in memory this sample can be.
    unsigned int maxsize;
    // The number of individual sample values this sample contains.
    unsigned int numsamples;
    // The loop point of the sample. If there is no loop point, this will be 0xFFFFFFFF.
    unsigned int sampleloop;
    // The format of the sample, either ALLOCATE_AUDIO_FORMAT_8BIT or ALLOCATE_AUDIO_FORMAT_16BIT.
    unsigned int format;
    // The sample rate of this sample.
    unsigned int samplerate;
    // A pointer to the next sample info structure if it exists.
    struct sample_info_struct *next;
} sample_info_t;

sample_info_t *find_sample(sample_info_t *head, uint32_t location)
{
    while (head != 0)
    {
        if ((head->flags & FLAGS_IN_USE) && head->location == location)
        {
            return head;
        }

        head = head->next;
    }

    return 0;
}

sample_info_t *new_sample(sample_info_t *head, uint32_t *location, unsigned int numsamples, unsigned int format, unsigned int samplerate)
{
    unsigned int size = (format == ALLOCATE_AUDIO_FORMAT_16BIT) ? (numsamples * 2) : numsamples;
    sample_info_t *orig = head;
    sample_info_t *last = 0;

    while (head != 0)
    {
        if (!(head->flags & FLAGS_IN_USE) && head->maxsize >= size)
        {
            // We can reuse this!
            head->flags = FLAGS_IN_USE;
            head->numsamples = numsamples;
            head->sampleloop = 0xFFFFFFFF;
            head->format = format;
            head->samplerate = samplerate;

            // Return the handle to this sample as well.
            if (location)
            {
                *location = head->location;
            }
            return orig;
        }

        last = head;
        head = head->next;
    }

    // We couldn't reuse any, so we need a new one!
    uint32_t spot = FIRST_SAMPLE_LOCATION;
    if (last)
    {
        // Grab the next 32-bit aligned memory location after the last sample.
        spot = ((last->location + last->maxsize) + 31) & 0xFFFFFFE0;
    }

    // DMA to us must be 32-byte aligned and in chunks of 32 bytes, so allocate
    // based on that knowledge.
    sample_info_t *new = (sample_info_t *)spot;
    new->flags = FLAGS_IN_USE;
    new->location = (spot + sizeof(sample_info_t) + 31) & 0xFFFFFFE0;
    new->maxsize = (size + 31) & 0xFFFFFFE0;
    new->numsamples = numsamples;
    new->sampleloop = 0xFFFFFFFF;
    new->format = format;
    new->samplerate = samplerate;
    new->next = 0;

    // Return the handle to this sample as well.
    if (location)
    {
        *location = new->location;
    }

    // Now, attach this to the end of the list.
    if (last)
    {
        last->next = new;
        return orig;
    }
    else
    {
        return new;
    }
}

// The maximum channels we have for triggered sounds to play. We reserve two at the
// top of the channel list for ringbuffer-style mixed sound from the SH-4.
#define MAX_CHANNELS 62

typedef struct
{
    // When this channel is guaranteed to be free, as compared to the millisecond timer.
    uint32_t free_time;
    // The currently playing sample, so we can free samples if need be.
    sample_info_t * sample;
} channel_info_t;

void main()
{
    // Set up our sample linked list.
    sample_info_t *samples = 0;
    uint32_t bookkeeping_timer = 0;

    // Reset AICA to a known state.
    aica_reset();

    // Reset our channel info trackers to a known state.
    channel_info_t channel_info[MAX_CHANNELS];
    memset(channel_info, 0, sizeof(channel_info_t) * MAX_CHANNELS);

    while( 1 )
    {
        // Update our uptime.
        AICA_CMD_BUFFER(CMD_BUFFER_UPTIME) = millisecond_timer;

        // See if the SH has requested that we perform some command.
        if (AICA_CMD_BUFFER(CMD_BUFFER_BUSY) != 0)
        {
            // Start by marking the response as a failure just in case we fail
            // to figure out the command below.
            AICA_CMD_BUFFER(CMD_BUFFER_RESPONSE) = RESPONSE_FAILURE;

            volatile uint32_t *params = &AICA_CMD_BUFFER(CMD_BUFFER_PARAMS);
            switch (AICA_CMD_BUFFER(CMD_BUFFER_REQUEST))
            {
                case REQUEST_SILENCE:
                {
                    // Request to shut up all channels.
                    aica_reset();

                    // None of the channels are playing anything anymore.
                    memset(channel_info, 0, sizeof(channel_info_t) * MAX_CHANNELS);

                    AICA_CMD_BUFFER(CMD_BUFFER_RESPONSE) = RESPONSE_SUCCESS;
                    break;
                }
                case REQUEST_ALLOCATE:
                {
                    // Request a spot to put a new sound of X bytes.
                    uint32_t numsamples = params[0];
                    uint32_t format = params[1];
                    uint32_t samplerate = params[2];

                    // Add a new sample.
                    uint32_t location = 0;
                    samples = new_sample(samples, &location, numsamples, format, samplerate);

                    // Return the location as a handle.
                    AICA_CMD_BUFFER(CMD_BUFFER_RESPONSE) = location;
                    break;
                }
                case REQUEST_FREE:
                {
                    // Free up a previous location.
                    uint32_t location = params[0];

                    sample_info_t *mysample = find_sample(samples, location);
                    if (mysample)
                    {
                        // Free it.
                        mysample->flags = 0;

                        // Free up any channels playing this sample.
                        for (unsigned int chan = 0; chan < MAX_CHANNELS; chan++)
                        {
                            if (channel_info[chan].sample == mysample)
                            {
                                aica_stop_sound(chan);
                                channel_info[chan].sample = 0;
                                channel_info[chan].free_time = 0;
                            }
                        }
                        AICA_CMD_BUFFER(CMD_BUFFER_RESPONSE) = RESPONSE_SUCCESS;
                    }

                    break;
                }
                case REQUEST_START_PLAY:
                {
                    // Find the sample to play by location.
                    uint32_t location = params[0];
                    uint32_t speakers = params[1];
                    uint32_t loudness = params[2];

                    sample_info_t *mysample = find_sample(samples, location);
                    if (mysample)
                    {
                        // Cool, found it, now assign it to a channel.
                        for (unsigned int chan = 0; chan < MAX_CHANNELS; chan++)
                        {
                            if (channel_info[chan].free_time <= millisecond_timer)
                            {
                                if (channel_info[chan].sample != 0 && (channel_info[chan].sample->flags & FLAGS_DISCARD_AFTER_USE))
                                {
                                    // Free old sample.
                                    channel_info[chan].sample->flags = 0;
                                    channel_info[chan].sample = 0;
                                }

                                // Calculate panning and format.
                                int pan = 0;
                                int format = 0;

                                // Start silent, and if they add some speakers to the bitmask then
                                // we update this to be the actual loudness from the above param.
                                int vol = VOL_MIN;

                                if (mysample->format == ALLOCATE_AUDIO_FORMAT_8BIT)
                                {
                                    format = FORMAT_8BIT;
                                }
                                else if (mysample->format == ALLOCATE_AUDIO_FORMAT_16BIT)
                                {
                                    format = FORMAT_16BIT;
                                }
                                else
                                {
                                    // Can't play this?
                                    break;
                                }

                                // We can use this channel.
                                channel_info[chan].sample = mysample;
                                if (mysample->sampleloop == 0xFFFFFFFF)
                                {
                                    channel_info[chan].free_time = millisecond_timer + ((mysample->numsamples * 1000) / mysample->samplerate) + 1;
                                }
                                else
                                {
                                    channel_info[chan].free_time = 0xFFFFFFFF;
                                }

                                if (speakers & ALLOCATE_SPEAKER_LEFT)
                                {
                                    if (speakers & ALLOCATE_SPEAKER_RIGHT)
                                    {
                                        pan = PAN_CENTER;
                                        vol = 255 - loudness;
                                    }
                                    else
                                    {
                                        pan = PAN_LEFT;
                                        vol = 255 - loudness;
                                    }
                                }
                                else if (speakers & ALLOCATE_SPEAKER_RIGHT)
                                {
                                    pan = PAN_RIGHT;
                                    vol = 255 - loudness;
                                }

                                // Actually play it!
                                if (mysample->sampleloop == 0xFFFFFFFF)
                                {
                                    aica_start_sound_oneshot(chan, (void *)mysample->location, format, mysample->numsamples, mysample->samplerate, vol, pan);
                                }
                                else
                                {
                                    aica_start_sound_loop(chan, (void *)mysample->location, format, mysample->numsamples, mysample->samplerate, vol, pan, mysample->sampleloop);
                                }
                                AICA_CMD_BUFFER(CMD_BUFFER_RESPONSE) = RESPONSE_SUCCESS;
                                break;
                            }
                        }
                    }
                    break;
                }
                case REQUEST_STOP_PLAY:
                {
                    // Find the sample to stop playing.
                    uint32_t location = params[0];

                    sample_info_t *mysample = find_sample(samples, location);
                    if (mysample)
                    {
                        for (unsigned int chan = 0; chan < MAX_CHANNELS; chan++)
                        {
                            if (channel_info[chan].sample == mysample)
                            {
                                aica_stop_sound(chan);
                                channel_info[chan].sample = 0;
                                channel_info[chan].free_time = 0;
                            }
                        }
                        if (mysample->flags & FLAGS_DISCARD_AFTER_USE)
                        {
                            // Free this sample.
                            mysample->flags = 0;
                        }

                        AICA_CMD_BUFFER(CMD_BUFFER_RESPONSE) = RESPONSE_SUCCESS;
                    }
                    break;
                }
                case REQUEST_DISCARD_AFTER_USE:
                {
                    // Find the sample to discard after playing.
                    uint32_t location = params[0];

                    sample_info_t *mysample = find_sample(samples, location);
                    if (mysample)
                    {
                        mysample->flags |= FLAGS_DISCARD_AFTER_USE;
                        AICA_CMD_BUFFER(CMD_BUFFER_RESPONSE) = RESPONSE_SUCCESS;
                    }
                    break;
                }
                case REQUEST_SET_LOOP_POINT:
                {
                    // Find the sample to mark as looping instead of one-shot.
                    uint32_t location = params[0];
                    uint32_t sampleloop = params[1];

                    sample_info_t *mysample = find_sample(samples, location);
                    if (mysample)
                    {
                        if (sampleloop < mysample->numsamples)
                        {
                            mysample->flags |= FLAGS_LOOP;
                            mysample->sampleloop = sampleloop;
                            AICA_CMD_BUFFER(CMD_BUFFER_RESPONSE) = RESPONSE_SUCCESS;
                        }
                    }
                    break;
                }
                case REQUEST_CLEAR_LOOP_POINT:
                {
                    // Find the sample to discard after playing.
                    uint32_t location = params[0];

                    sample_info_t *mysample = find_sample(samples, location);
                    if (mysample)
                    {
                        mysample->flags &= (~FLAGS_LOOP);
                        mysample->sampleloop = 0xFFFFFFFF;
                        AICA_CMD_BUFFER(CMD_BUFFER_RESPONSE) = RESPONSE_SUCCESS;
                    }
                    break;
                }
            }

            // Acknowledge command received.
            AICA_CMD_BUFFER(CMD_BUFFER_BUSY) = 0;
        }

        // Bookkeeping.
        if (bookkeeping_timer != millisecond_timer)
        {
            bookkeeping_timer = millisecond_timer;
            for (unsigned int chan = 0; chan < MAX_CHANNELS; chan++)
            {
                if (channel_info[chan].free_time <= millisecond_timer && channel_info[chan].sample != 0)
                {
                    // See if this sample needs freeing.
                    if (channel_info[chan].sample->flags & FLAGS_DISCARD_AFTER_USE)
                    {
                        // It does!
                        channel_info[chan].sample->flags = 0;
                    }

                    // This channel doesn't need a reference to the sample anymore.
                    channel_info[chan].sample = 0;
                }
            }
        }
    }
}
