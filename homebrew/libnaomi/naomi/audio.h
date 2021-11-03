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

#ifdef __cplusplus
}
#endif

#endif
