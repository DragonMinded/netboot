#ifndef __EEPROM_H
#define __EEPROM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define ATTRACT_SOUNDS_OFF 0
#define ATTRACT_SOUNDS_ON 1

#define MONITOR_ORIENTATION_HORIZONTAL 0
#define MONITOR_ORIENTATION_VERTICAL 1

#define COIN_CHUTE_COMMON 0
#define COIN_CHUTE_INDIVIDUAL 1

#define COIN_ASSIGNMENT_FREE_PLAY 27
#define COIN_ASSIGNMENT_MANUAL 28

typedef struct eeprom_system
{
    // Serial number of your game, should match the ROM header serial.
    uint8_t serial[4];

    // Whether attract sounds are on or off. Use the above sound defines.
    unsigned int attract_sounds;

    // Monitor orientation. Use the above monitor orientation defines.
    unsigned int monitor_orientation;

    // Number of players, as an integer between 1 and 4.
    unsigned int players;

    // Coin chute setting. Use the above coin chute defines.
    unsigned int chute_setting;

    // Coin assignments. Use the coin assignment defines for free play and
    // manual assignment, or an integer between 1-26 for a setting number.
    unsigned int coin_assignment;

    // Coins per credit. Accepts values 1-9. Only matters when coin assignment
    // is manual.
    unsigned int coins_per_credit;

    // Chute 1 multiplier. Accepts values 1-9. Only matters when coin assingment
    // is manual.
    unsigned int chute_1_multiplier;

    // Chute 2 multiplier. Accepts values 1-9. Only matters when coin assingment
    // is manual.
    unsigned int chute_2_multiplier;

    // Bonus coin on X credits inserted. Accepts values 0 and 2-9. Only matters
    // when coin assignment is manual.
    unsigned int bonus_coin;

    // Sequence numbers. This is the sequence text to choose for all 8 sequences.
    unsigned int sequences[8];
} eeprom_system_t;

#define MAXIMUM_GAME_SETTINGS_LENGTH 42

typedef struct eeprom_game
{
    // The size of the game EEPROM section. Can be 0 if there
    // is no game EEPROM section or the section is invalid.
    // Can be 1-42 bytes long if it is valid, up to the game
    // that needs to write/read settings for the length and
    // its layout.
    unsigned int size;
    // The EEPROM data for the game section. This will be CRC'd
    // automatically. It is not necessary to use the whole amount,
    // as only the first size bytes will be valid.
    uint8_t data[MAXIMUM_GAME_SETTINGS_LENGTH];
} eeprom_game_t;

typedef struct eeprom
{
    // System settings, such as monitor orientation, number of
    // players, coin assignments, etc.
    eeprom_system_t system;
    // Game settings, up to you to determine the layout.
    eeprom_game_t game;
} eeprom_t;

// API for working with parsed data without worry. If the eeprom was successfully
// read or written, returns zero. If the eeprom was not able to be read or written
// then returns nonzero. Note that if you attempt to read the eeprom from multiple
// threads it is indeterminate which one will succeed. Therefore it is recommended
// to keep reading and writing to one thread.
int eeprom_read(eeprom_t *eeprom);
int eeprom_write(eeprom_t *eeprom);

// Helper function to return the current executing software's serial
// as specified in the header and build scripts.
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

// Given a chunk of raw data and a length in bytes, return the 16-bit
// CRC over that data suitable for writing in an EEPROM.
uint16_t eeprom_crc(uint8_t *data, unsigned int len);

// Defines for the bank argument for the below functions.
#define EEPROM_BANK_1 1
#define EEPROM_BANK_2 2

// Given a 128-byte EEPROM file in data, returns true if the system
// chunk is valid for a given bank. Under normal circumstances, both
// banks should be valid and contain the same data. However, if power
// is lost during a write, one of the banks could become corrupted.
int eeprom_system_valid(uint8_t *data, int bank);

// Given a 128-byte EEPROM file in data, retursn true if the game
// chunk is valid for a given bank. Much like the system chunk, this
// should always be true for both banks unless there was a failure
// mid-write. Note that a blank game section (All 0xFF) is not
// considered a valid game EEPROM and will return false here.
int eeprom_game_valid(uint8_t *data, int bank);

// Given a 128-byte EEPROM file in data, returns true if the EEPROM
// passes the syste and game validity checks for at least one bank.
// Note that not necessarily the same bank might be bad during a failure
// to write as they are addressed independently. Note also that if
// the game section is blank, this is considered valid for the purpose
// of verifying that an entire EEPROM image is acceptable or not.
int eeprom_valid(uint8_t *data);

// Given a 128-byte EEPROM file in data and a pointer to an eeprom_t
// srtucture, parses the data from the EEPROM file into the parsed
// eeprom_t structure. If a part of the EEPROM (either system or game)
// has no valid bank it fills in the defaults for that bank. Otherwise
// it fills in data from the first valid bank it finds.
void parse_eeprom(uint8_t *data, eeprom_t *eeprom);

// Given a 128-byte EEPROM file in data and a pointer to an eeprom_t
// structure, updates the EEPROM file to contain the data in the
// eeprom_t structure.
void unparse_eeprom(uint8_t *data, eeprom_t *eeprom);

#ifdef __cplusplus
}
#endif

#endif
