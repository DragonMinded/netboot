#ifndef __EEPROM_H
#define __EEPROM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

uint8_t *eeprom_serial();

// Size of an EEPROM.
#define EEPROM_SIZE 128

// Location of the two system data chunks inside the EEPROM.
#define SYSTEM_CHUNK_1 0
#define SYSTEM_CHUNK_2 18

// Location of the system chunk itself inside the EEPROM.
#define SYSTEM_SECTION 0
#define SYSTEM_LENGTH 36

// Location of the game chunk itself inside the EEPROM.
#define GAME_SECTION 36
#define GAME_LENGTH 92

// Location of various important data bits within system chunks.
#define SYSTEM_CRC_LOC 0
#define SYSTEM_CRC_SIZE 2
#define SYSTEM_SERIAL_LOC 3
#define SYSTEM_SERIAL_SIZE 4
#define SYSTEM_CRC_REGION_LOC 2
#define SYSTEM_CRC_REGION_SIZE 16

// Location of the two game data header chunks inside the EEPROM.
#define GAME_CHUNK_1 36
#define GAME_CHUNK_2 40
#define GAME_PAYLOAD 44

// Location of various important data bits within game chunks.
#define GAME_CRC_LOC 0
#define GAME_CRC_SIZE 2
#define GAME_LEN_LOC_1 2
#define GAME_LEN_LOC_2 3

// API for working with raw eeprom chunks.
uint16_t eeprom_crc(uint8_t *data, unsigned int len);
int eeprom_system_valid(uint8_t *data);
int eeprom_game_valid(uint8_t *data);

void maple_init();
void maple_free();
int maple_request_eeprom_read(uint8_t *outbytes);
int maple_request_eeprom_write(uint8_t *inbytes);

#ifdef __cplusplus
}
#endif

#endif
