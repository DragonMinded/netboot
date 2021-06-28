#ifndef __AUDIO_H
#define __AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define AICA_BASE 0xA0700000

#define AICA_VERSION (0x2800 >> 2)
#define AICA_RESET (0x2C00 >> 2)

extern uint8_t *aica_bin_data;
extern unsigned int aica_bin_len;

#define AICA_DEFAULT_BINARY aica_bin_data
#define AICA_DEFAULT_BINARY_SIZE aica_bin_len

void load_aica_binary(void *binary, unsigned int length);

#ifdef __cplusplus
}
#endif

#endif
