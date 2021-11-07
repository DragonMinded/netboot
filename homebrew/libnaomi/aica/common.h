#ifndef __AICA_COMMON_H
#define __AICA_COMMON_H

#include <stdint.h>

// Locations for various "registers".
#define CMD_BUFFER_UPTIME 0x0
#define CMD_BUFFER_BUSY 0x4
#define CMD_BUFFER_REQUEST 0x8
#define CMD_BUFFER_PARAMS 0xC
#define CMD_BUFFER_RESPONSE 0x20

// Request "register" valid values.
#define REQUEST_SILENCE 0x1000
#define REQUEST_ALLOCATE 0x1001
#define REQUEST_FREE 0x1002
#define REQUEST_START_PLAY 0x1003

// Response "register" enumerated values.
#define RESPONSE_FAILURE 0x0
#define RESPONSE_SUCCESS 0x1

// Constants for the REQUEST_ALLOCATE parameter.
#define ALLOCATE_AUDIO_FORMAT_8BIT 0
#define ALLOCATE_AUDIO_FORMAT_16BIT 1

#define ALLOCATE_SPEAKER_LEFT 1
#define ALLOCATE_SPEAKER_RIGHT 2

#endif
