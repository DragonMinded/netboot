#include <stdint.h>

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
#define AICA_UNKNOWN1 (0x2040 >> 2)
#define AICA_UNKNOWN2 (0x2044 >> 2)
#define AICA_VERSION (0x2800 >> 2)
#define AICA_RINGBUF (0x2804 >> 2)

void aica_reset()
{
    volatile uint32_t *aicabase = (volatile uint32_t *)AICA_BASE;

    // Set master DAC volume to 0 when initializing registers.
    aicabase[AICA_VERSION] = aicabase[AICA_VERSION] & 0xFFFFFFF0;

    /* Reset all 64 channels to a silent state */
    for(unsigned int chan = 0; chan < 64; chan++)
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

void aica_start_sound(int channel, void *data, int format, int num_samples, int freq, int vol, int pan)
{
    volatile uint32_t *aicabase = (volatile uint32_t *)AICA_BASE;

    unsigned long freq_lo = 0;
    unsigned long freq_base = 5644800;
    int freq_hi = 7;

    /* Set sample format and buffer address */
    aicabase[CHANNEL(channel, AICA_CFG_ADDR_HIGH)] = ((format & 0x3) << 7) | ((((unsigned long)data) >> 16) & 0x7F);
    aicabase[CHANNEL(channel, AICA_CFG_ADDR_LOW)] = ((unsigned long)data) & 0xFFFF;

    /* Number of samples */
    aicabase[CHANNEL(channel, AICA_CFG_LOOP_START)] = 0;
    aicabase[CHANNEL(channel, AICA_CFG_LOOP_END)] = num_samples;

    /* Need to figure out frequency conversion */
    aicabase[CHANNEL(channel, AICA_CFG_PITCH)] = 0;

    /* Set volume, pan, and some other stuff */
    aicabase[CHANNEL(channel, AICA_CFG_PAN_VOLUME)] = (pan & 0x1F) | (0xD << 8);
    aicabase[CHANNEL(channel, AICA_CFG_VOLUME2)] = 0x20 | ((vol & 0xFF) << 8);
    aicabase[CHANNEL(channel, AICA_CFG_ADSR1)] = 0x001F;
    aicabase[CHANNEL(channel, AICA_CFG_ADSR2)] = 0x001F;
    aicabase[CHANNEL(channel, AICA_CFG_LFO1)] = 0x8000;  // BIOS sets this to 0x8000??
    aicabase[CHANNEL(channel, AICA_CFG_LFO2)] = 0;  // BIOS only sets bottom 8 bits to 0??

    /* Enable playback */
    aicabase[CHANNEL(channel, AICA_CFG_ADDR_HIGH)] = (aicabase[CHANNEL(channel, AICA_CFG_ADDR_HIGH)] & 0x3FFF) | 0x4000;
    aicabase[CHANNEL(channel, AICA_CFG_LFO1)] = 0x0000;  // BIOS sets this to 0x0000 now??
    aicabase[CHANNEL(channel, AICA_CFG_ADDR_HIGH)] = (aicabase[CHANNEL(channel, AICA_CFG_ADDR_HIGH)] & 0x3FFF) | 0xC000;
}

void aica_stop_sound(int channel)
{
    volatile uint32_t *aicabase = (volatile uint32_t *)AICA_BASE;
   
    aicabase[CHANNEL(channel, AICA_CFG_ADDR_HIGH)] = (aicabase[CHANNEL(channel, AICA_CFG_ADDR_HIGH)] & (~0xC000)) | 0x8000;
}

void main()
{
    volatile uint32_t *status = (volatile uint32_t *)0xF100;
    status[0] = 0x12340000;
    aica_reset();

    extern uint8_t *success_raw_data;
    extern unsigned int success_raw_len;

    status[0] = 0x56780000;

    aica_start_sound(0, success_raw_data, FORMAT_8BIT, success_raw_len, 44100, 0x1D, PAN_CENTER);

    status[0] = 0x9ABC0000;
    while( 1 )
    {
        status[0] = (status[0] & 0xFFFF0000) | (((status[0] & 0xFFFF) + 1) & 0xFFFF);
    }
}
