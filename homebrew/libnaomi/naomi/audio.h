#ifndef __AUDIO_H
#define __AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

extern uint8_t *aica_bin_data;
extern unsigned int aica_bin_len;

#define AICA_DEFAULT_BINARY aica_bin_data
#define AICA_DEFAULT_BINARY_SIZE aica_bin_len

// This interface is intentionally not thread-safe. If more than one thread
// attempts to load a new binary at once it is indeterminate which one will
// be loaded on the AICA. It is recommended not to interact with the AICA
// across multiple threads.
void load_aica_binary(void *binary, unsigned int length);

// Initialize and free the default audio interface which allows you to use all
// of the below fuctions.
void audio_init();
void audio_free();

// Diagnostic function to grab the uptime of the AICA in milliseconds.
uint32_t audio_aica_uptime();

#define AUDIO_FORMAT_8BIT 0
#define AUDIO_FORMAT_16BIT 1

#define SPEAKER_LEFT 1
#define SPEAKER_RIGHT 2

// Play a sound. This will move the sound to the correct location for the AICA
// to see it and then request it to be played on any available audio channel.
// The format should be one of the above audio format defines, the samplerate
// should be an integer between 8000-96000, and the speakers should be a bitmask
// of the above speaker constants. Returns 0 on success, and a negative number
// to indicate that there were no available channels to play on.
int audio_play_sound(int format, unsigned int samplerate, uint32_t speakers, void *data, unsigned int num_samples);

// Register a sound to be played later. This will move the soudn to the correct
// location for the AICA to see it and then return a handle for later playback
// of the sound. If you are going to play a sound repeatedly, this is more
// efficient than just playing the sound directly. The format should be one of
// the above audio format defines, the samplerate should be an integer between
// 8000-96000, and the speakers should be a bitmask of the above speaker constants.
// Returns a negative number if there was no room to register the sound.
int audio_register_sound(int format, unsigned int samplerate, uint32_t speakers, void *data, unsigned int num_samples);

// Unregister a previously registered sound, freeing that spot up for potentially
// a new sound in the future. Passing it a negative number is the same as a noop.
void audio_unregister_sound(int sound);

// Play a previously-registered sound. This will be played on any available audio
// channel. Passing it a negative number is the same as a noop.
void audio_play_registered_sound(int sound);

#ifdef __cplusplus
}
#endif

#endif
