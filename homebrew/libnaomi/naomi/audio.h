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
// of the above speaker constants. The volume should be a floating point number
// between 0.0 and 1.0 inclusive for how loud to play the sample (completely silent
// to full volume). Returns 0 on success, and a negative number to indicate that
// there were no available channels to play on.
int audio_play_sound(int format, unsigned int samplerate, uint32_t speakers, float volume, void *data, unsigned int num_samples);

// Register a sound to be played later. This will move the soudn to the correct
// location for the AICA to see it and then return a handle for later playback
// of the sound. If you are going to play a sound repeatedly, this is more
// efficient than just playing the sound directly. The format should be one of
// the above audio format defines, the samplerate should be an integer between
// 8000-96000, and the speakers should be a bitmask of the above speaker constants.
// Returns a negative number if there was no room to register the sound.
int audio_register_sound(int format, unsigned int samplerate, void *data, unsigned int num_samples);

// Unregister a previously registered sound, freeing that spot up for potentially
// a new sound in the future. Passing it a negative number is the same as a noop.
void audio_unregister_sound(int sound);

// Play a previously-registered sound. This will be played on any available audio
// channel. Passing it a negative number will perform a noop with a negative return.
// Returns zero on success, or a negative value if there were no available channels.
// You can call this as many times as you wand and that many copies of the sound
// will play up to the hardware limit of simultaneous sounds.
int audio_play_registered_sound(int sound, uint32_t speakers, float volume);

// Stop every playing instance of a previously-registered sound. Returns zero on
// success, or a negative value on failure. If you pass it a negative sound it will
// behave like a noop except for returning failure.
int audio_stop_registered_sound(int sound);

// Requests that all subsequent audio_play_registered_sound() calls for this particular
// registered sound be looped at a particular loop point. The loop point must be between
// 0 and the number of samples in the sound. Set the loop point to 0 to repeat the
// entire sound. Note that existing sounds that are currently playing will not be
// converted to looping versions of the sound.
int audio_set_registered_sound_loop(int sound, unsigned int loop_point);

// Requests that all subsequent audio_play_registered_sound() calls for this particular
// registered sound not be looped. Note that existing sounds that are currently looping
// will not be converted to one-shot sounds.
int audio_clear_registered_sound_loop(int sound);

// Registers a stereo ringbuffer which can be written to for software-mixed sounds.
// Returns 0 on successful ringbuffer initialization or a negative number on failure.
// The format should be identical to audio_play_sound()/audio_register_sound(), and is
// the format of data you will be writing later. The samplerate is the playback speed
// of the buffer in hz. The num_samples is how many samples you want available in your
// ringbuffer. Higher means less chance of underflow but higher latency on audio output.
// Care should be taken to ensure that num_samples is always divisible by 4.
int audio_register_ringbuffer(int format, unsigned int samplerate, unsigned int num_samples);

// Frees up a previously registered stereo ringbuffer.
void audio_unregister_ringbuffer();

// Write interleaved stereo data to the ringbuffer. The number of samples here is the
// number of stereo sample pairs. It is assumed that the left channel comes first, then
// the right. Returns the number of sample pairs that were successfully written.
int audio_write_stereo_data(void *data, unsigned int num_samples);

// Constants for the channel input to the below function.
#define AUDIO_CHANNEL_LEFT 0
#define AUDIO_CHANNEL_RIGHT 1

// Write mono data to a particular channel in the ringbuffer. The number of samples here
// is the number of individual samples for the channel. Returns the number of samples
// that were successfully written.
int audio_write_mono_data(int channel, void *data, unsigned int num_samples);

#ifdef __cplusplus
}
#endif

#endif
