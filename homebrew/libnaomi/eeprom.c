#include <stdint.h>
#include <string.h>
#include "naomi/eeprom.h"
#include "naomi/maple.h"

uint32_t eeprom_crc_inner(uint32_t running_crc, uint8_t next_byte)
{
    // First, mask off the values so we don't get a collision
    running_crc &= 0xFFFFFF00;

    // Add the byte into the CRC
    running_crc = running_crc | next_byte;

    // Now, run the algorithm across the new byte
    for (int i = 0; i < 8; i++)
    {
        if (running_crc < 0x80000000)
        {
            running_crc = running_crc << 1;
        }
        else
        {
            running_crc = (running_crc << 1) + 0x10210000;
        }
    }

    return running_crc;
}

uint16_t eeprom_crc(uint8_t *data, unsigned int len)
{
    uint32_t running_crc = 0xDEBDEB00;

    // CRC over all the data we've been given.
    for (unsigned int i = 0; i < len; i++)
    {
        running_crc = eeprom_crc_inner(running_crc, data[i]);
    }

    // Add in the null byte that Naomi BIOS seems to want.
    running_crc = eeprom_crc_inner(running_crc, 0);

    // Calculate the final CRC value by taking the remainder.
    return (running_crc >> 16) & 0xFFFF;
}

int eeprom_system_valid(uint8_t *data, int bank)
{
    // Calculate first system chunk.
    if (bank == EEPROM_BANK_1)
    {
        uint16_t expected = 0;
        memcpy(&expected, data + SYSTEM_CHUNK_1 + SYSTEM_CRC_LOC, SYSTEM_CRC_SIZE);
        if (expected != eeprom_crc(data + SYSTEM_CHUNK_1 + SYSTEM_CRC_REGION_LOC, SYSTEM_CRC_REGION_SIZE))
        {
            return 0;
        }
        else
        {
            return 1;
        }
    }

    // Calculate second system chunk.
    if (bank == EEPROM_BANK_2)
    {
        uint16_t expected = 0;
        memcpy(&expected, data + SYSTEM_CHUNK_2 + SYSTEM_CRC_LOC, SYSTEM_CRC_SIZE);
        if (expected != eeprom_crc(data + SYSTEM_CHUNK_2 + SYSTEM_CRC_REGION_LOC, SYSTEM_CRC_REGION_SIZE))
        {
            return 0;
        }
        else
        {
            return 1;
        }
    }

    // Unknown bank??
    return 0;
}

int eeprom_game_valid(uint8_t *data, int bank)
{
    // Calculate first game chunk.
    if (bank == EEPROM_BANK_1)
    {
        // Verify the lengths are correct.
        if (data[GAME_CHUNK_1 + GAME_LEN_LOC_1] != data[GAME_CHUNK_1 + GAME_LEN_LOC_2])
        {
            return 0;
        }

        // Verify the CRC over the data is correct.
        uint16_t expected = 0;
        memcpy(&expected, data + GAME_CHUNK_1 + GAME_CRC_LOC, GAME_CRC_SIZE);
        if (expected != eeprom_crc(data + GAME_PAYLOAD, data[GAME_CHUNK_1 + GAME_LEN_LOC_1]))
        {
            return 0;
        }
        else
        {
            return 1;
        }
    }

    // Calculate second game chunk.
    if (bank == EEPROM_BANK_2)
    {
        // Verify the lengths are correct.
        if (data[GAME_CHUNK_2 + GAME_LEN_LOC_1] != data[GAME_CHUNK_2 + GAME_LEN_LOC_2])
        {
            return 0;
        }

        // Now, this gets complicated. Since we need to know how far to go to get
        // to the second chunk (its based on the first chunk's size), we need to
        // see if the first chunk agrees with our leigth. However, we can't just
        // wholesale compare since its possible that the first chunk got corrupted
        // and it did so in the length section. So, we only pass if at least one
        // of the two length bytes matches our length. We know that it is "correct"
        // because if we got here, both our lengths match.
        if (
            data[GAME_CHUNK_2 + GAME_LEN_LOC_1] != data[GAME_CHUNK_1 + GAME_LEN_LOC_1] &&
            data[GAME_CHUNK_2 + GAME_LEN_LOC_1] != data[GAME_CHUNK_1 + GAME_LEN_LOC_2]
        ) {
            // It matched neither, so we are really not sure what to believe. Its
            // not likely that a write error would affect two bytes at once since
            // the EEPROM is programmed serially, one byte at a time.
            return 0;
        }

        // Verify the CRC over the data is correct.
        uint16_t expected = 0;
        memcpy(&expected, data + GAME_CHUNK_2 + GAME_CRC_LOC, GAME_CRC_SIZE);
        if (expected != eeprom_crc(data + GAME_PAYLOAD + data[GAME_CHUNK_2 + GAME_LEN_LOC_1], data[GAME_CHUNK_2 + GAME_LEN_LOC_1]))
        {
            return 0;
        }
        else
        {
            return 1;
        }
    }

    // Unknown bank??
    return 0;
}

int eeprom_valid(uint8_t *data)
{
    if (eeprom_system_valid(data, EEPROM_BANK_1) == 0 && eeprom_system_valid(data, EEPROM_BANK_2) == 0)
    {
        // Failed both system chunks.
        return 0;
    }

    if (eeprom_game_valid(data, EEPROM_BANK_1) != 0 || eeprom_game_valid(data, EEPROM_BANK_2) != 0)
    {
        // Passed at least one game chunk.
        return 1;
    }

    // We failed both game chunks. Make sure they are blank.
    if (
        data[GAME_CHUNK_1 + GAME_LEN_LOC_1] == 0xFF &&
        data[GAME_CHUNK_1 + GAME_LEN_LOC_2] == 0xFF &&
        data[GAME_CHUNK_2 + GAME_LEN_LOC_1] == 0xFF &&
        data[GAME_CHUNK_2 + GAME_LEN_LOC_2] == 0xFF
    )
    {
        // Game chunk is blank, this is a valid EEPROM.
        return 1;
    }
    else
    {
        // Game chunk is not blank, but both failed the checks.
        return 0;
    }
}

void parse_eeprom(uint8_t *data, eeprom_t *eeprom)
{
    // This gets a bit complicated because we want to parse out of the bank that is valid for
    // each. If there is no valid bank for either, then we want to return defaults for that
    // chunk.
    static const int whichbank[2] = { EEPROM_BANK_1, EEPROM_BANK_2 };
    static const int whichsystemchunk[2] = { SYSTEM_CHUNK_1, SYSTEM_CHUNK_2 };
    static const int whichgamechunk[2] = { GAME_CHUNK_1, GAME_CHUNK_2 };

    int parsed = 0;
    for (int bankno = 0; bankno < 2; bankno++)
    {
        int bank = whichbank[bankno];
        int systemchunk = whichsystemchunk[bankno];

        if (eeprom_system_valid(data, bank))
        {
            // Okay, we can parse out the data to get the actual values.
            eeprom->system.attract_sounds = ((data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 0] >> 4) & 0xF) ? ATTRACT_SOUNDS_ON : ATTRACT_SOUNDS_OFF;
            eeprom->system.monitor_orientation = (data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 0] & 0xF) ? MONITOR_ORIENTATION_VERTICAL : MONITOR_ORIENTATION_HORIZONTAL;
            memcpy(eeprom->system.serial, &data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 1], 4);

            eeprom->system.players = ((data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 6] >> 4) & 0xF) + 1;
            if (eeprom->system.players > 4) {
                eeprom->system.players = 2;
            }
            eeprom->system.chute_setting = (data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 6] & 0xF) ? COIN_CHUTE_INDIVIDUAL : COIN_CHUTE_COMMON;

            eeprom->system.coin_assignment = data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 7] + 1;
            if (eeprom->system.coin_assignment > COIN_ASSIGNMENT_MANUAL) {
                eeprom->system.coin_assignment = COIN_ASSIGNMENT_MANUAL;
            }

            eeprom->system.coins_per_credit = data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 8];
            if (eeprom->system.coins_per_credit < 1 || eeprom->system.coins_per_credit > 9) {
                eeprom->system.coins_per_credit = 1;
            }

            eeprom->system.chute_1_multiplier = data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 9];
            if (eeprom->system.chute_1_multiplier < 1 || eeprom->system.chute_1_multiplier > 9) {
                eeprom->system.chute_1_multiplier = 1;
            }

            eeprom->system.chute_2_multiplier = data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 10];
            if (eeprom->system.chute_2_multiplier < 1 || eeprom->system.chute_2_multiplier > 9) {
                eeprom->system.chute_2_multiplier = 1;
            }

            eeprom->system.bonus_coin = data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 11];
            if (eeprom->system.bonus_coin == 1 || eeprom->system.bonus_coin > 9) {
                eeprom->system.bonus_coin = 0;
            }

            eeprom->system.sequences[0] = ((data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 12] >> 4) & 0xF);
            eeprom->system.sequences[1] = (data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 12] & 0xF);
            eeprom->system.sequences[2] = ((data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 13] >> 4) & 0xF);
            eeprom->system.sequences[3] = (data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 13] & 0xF);
            eeprom->system.sequences[4] = ((data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 14] >> 4) & 0xF);
            eeprom->system.sequences[5] = (data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 14] & 0xF);
            eeprom->system.sequences[6] = ((data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 15] >> 4) & 0xF);
            eeprom->system.sequences[7] = (data[systemchunk + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE + 15] & 0xF);

            for (int i = 0; i < 8; i++)
            {
                if (eeprom->system.sequences[i] < 1 || eeprom->system.sequences[i] > 5) {
                    eeprom->system.sequences[i] = 1;
                }
            }

            // We successfully parsed, don't bother with the next bank.
            parsed = 1;
            break;
        }
    }

    if (parsed == 0)
    {
        // Let's set up defaults. This shouldn't happen if the BIOS
        // has properly run before us, but its conceivable somebody
        // messed with the EEPROM directly.
        memcpy(eeprom->system.serial, eeprom_serial(), 4);
        eeprom->system.attract_sounds = ATTRACT_SOUNDS_ON;
        eeprom->system.monitor_orientation = MONITOR_ORIENTATION_HORIZONTAL;
        eeprom->system.players = 2;
        eeprom->system.chute_setting = COIN_CHUTE_COMMON;
        eeprom->system.coin_assignment = 1;
        eeprom->system.coins_per_credit = 1;
        eeprom->system.chute_1_multiplier = 1;
        eeprom->system.chute_2_multiplier = 1;
        eeprom->system.bonus_coin = 0;

        for (int i = 0; i < 8; i++)
        {
            eeprom->system.sequences[i] = 1;
        }
    }

    parsed = 0;
    for (int bankno = 0; bankno < 2; bankno++)
    {
        int bank = whichbank[bankno];
        int gamechunk = whichgamechunk[bankno];

        if (eeprom_game_valid(data, bank))
        {
            // Game is valid, copy the data we care about. If it is the second
            // bank, then we know from passing the above validity check that
            // the length matches at least one of the lengths from the first
            // bank and that the CRC matches, so its valid to index that far
            // even while having the first bank be wrong.
            eeprom->game.size = data[gamechunk + GAME_LEN_LOC_1];
            if (eeprom->game.size > MAXIMUM_GAME_SETTINGS_LENGTH)
            {
                eeprom->game.size = MAXIMUM_GAME_SETTINGS_LENGTH;
            }
            memset(eeprom->game.data, 0, MAXIMUM_GAME_SETTINGS_LENGTH);
            memcpy(eeprom->game.data, data + GAME_PAYLOAD + (gamechunk == GAME_CHUNK_1 ? 0 : data[GAME_CHUNK_2 + GAME_LEN_LOC_1]), eeprom->game.size);

            // Successfully parsed this one!
            parsed = 1;
            break;
        }
    }

    if (parsed == 0)
    {
        // Game is not valid, set the size to 0 and move on.
        eeprom->game.size = 0;
        memset(eeprom->game.data, 0, MAXIMUM_GAME_SETTINGS_LENGTH);
    }
}

void unparse_eeprom(uint8_t *data, eeprom_t *eeprom)
{
    // First, make sure that we set up the full data, even bytes we don't touch.
    memset(data, 0xFF, 128);

    // Now, unparse the system settings.
    uint8_t system[16];

    if (eeprom->system.attract_sounds == ATTRACT_SOUNDS_ON) {
        system[0] = 0x10 | (system[0] & 0x0F);
    } else {
        system[0] = 0x00 | (system[0] & 0x0F);
    }
    if (eeprom->system.monitor_orientation == MONITOR_ORIENTATION_VERTICAL) {
        system[0] = 0x01 | (system[0] & 0xF0);
    } else {
        system[0] = 0x00 | (system[0] & 0xF0);
    }

    memcpy(&system[1], eeprom->system.serial, 4);

    if (eeprom->system.players < 1 || eeprom->system.players > 4) {
        system[6] = 0x10 | (system[6] & 0x0F);
    } else {
        system[6] = (((eeprom->system.players - 1) << 4) & 0xF0) | (system[6] & 0x0F);
    }
    if (eeprom->system.chute_setting == COIN_CHUTE_INDIVIDUAL) {
        system[6] = 0x01 | (system[6] & 0xF0);
    } else {
        system[6] = 0x00 | (system[6] & 0xF0);
    }

    if (eeprom->system.coin_assignment < 1 || eeprom->system.coin_assignment > COIN_ASSIGNMENT_MANUAL) {
        system[7] = 0;
    } else {
        system[7] = eeprom->system.coin_assignment - 1;
    }

    if (eeprom->system.coins_per_credit < 1 || eeprom->system.coins_per_credit > 9) {
        system[8] = 1;
    } else {
        system[8] = eeprom->system.coins_per_credit;
    }

    if (eeprom->system.chute_1_multiplier < 1 || eeprom->system.chute_1_multiplier > 9) {
        system[9] = 1;
    } else {
        system[9] = eeprom->system.chute_1_multiplier;
    }

    if (eeprom->system.chute_2_multiplier < 1 || eeprom->system.chute_2_multiplier > 9) {
        system[10] = 1;
    } else {
        system[10] = eeprom->system.chute_2_multiplier;
    }

    if (eeprom->system.bonus_coin == 1 || eeprom->system.bonus_coin > 9) {
        system[11] = 0;
    } else {
        system[11] = eeprom->system.bonus_coin;
    }

    unsigned int sequences[8];
    for (int i = 0; i < 8; i++) {
        if (eeprom->system.sequences[i] < 1 || eeprom->system.sequences[i] > 5) {
            sequences[i] = 1;
        } else {
            sequences[i] = eeprom->system.sequences[i];
        }
    }

    system[12] = ((sequences[0] << 4) & 0xF0) | (sequences[1] & 0x0F);
    system[13] = ((sequences[2] << 4) & 0xF0) | (sequences[3] & 0x0F);
    system[14] = ((sequences[4] << 4) & 0xF0) | (sequences[5] & 0x0F);
    system[15] = ((sequences[6] << 4) & 0xF0) | (sequences[7] & 0x0F);

    // Now, get the CRC.
    uint16_t crc = eeprom_crc(system, SYSTEM_CRC_REGION_SIZE);

    // Copy the chunks we generated over.
    memcpy(&data[SYSTEM_CHUNK_1 + SYSTEM_CRC_LOC], &crc, SYSTEM_CRC_SIZE);
    memcpy(&data[SYSTEM_CHUNK_1 + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE], system, SYSTEM_CRC_REGION_SIZE);
    memcpy(&data[SYSTEM_CHUNK_2 + SYSTEM_CRC_LOC], &crc, SYSTEM_CRC_SIZE);
    memcpy(&data[SYSTEM_CHUNK_2 + SYSTEM_CRC_LOC + SYSTEM_CRC_SIZE], system, SYSTEM_CRC_REGION_SIZE);

    // Finally, unparse the game settings but only if they exist.
    if (eeprom->game.size > 0) {
        unsigned int size = eeprom->game.size;

        if (size > MAXIMUM_GAME_SETTINGS_LENGTH) {
            size = MAXIMUM_GAME_SETTINGS_LENGTH;
        }

        // CRC the data first off.
        crc = eeprom_crc(eeprom->game.data, size);

        // Now, copy the data to the right bytes.
        memcpy(&data[GAME_CHUNK_1 + GAME_CRC_LOC], &crc, GAME_CRC_SIZE);
        data[GAME_CHUNK_1 + GAME_LEN_LOC_1] = size;
        data[GAME_CHUNK_1 + GAME_LEN_LOC_2] = size;

        memcpy(&data[GAME_CHUNK_2 + GAME_CRC_LOC], &crc, GAME_CRC_SIZE);
        data[GAME_CHUNK_2 + GAME_LEN_LOC_1] = size;
        data[GAME_CHUNK_2 + GAME_LEN_LOC_2] = size;

        memcpy(&data[GAME_PAYLOAD], eeprom->game.data, size);
        memcpy(&data[GAME_PAYLOAD + size], eeprom->game.data, size);
    }
}

int eeprom_read(eeprom_t *eeprom)
{
    uint8_t data[128];

    // First, attempt to read from the actual chip.
    if(maple_request_eeprom_read(data) == 0)
    {
        // Parse the data.
        parse_eeprom(data, eeprom);

        // Return success.
        return 0;
    }
    else
    {
        // Failed to read the eeprom, can't return settings.
        return 1;
    }
}

int eeprom_write(eeprom_t *eeprom)
{
    // First, unparse the data.
    uint8_t data[128];
    unparse_eeprom(data, eeprom);

    //  Now, write it to the actual chip.
    return maple_request_eeprom_write(data);
}

uint8_t *eeprom_serial()
{
    static unsigned int initialized = 0;
    static uint8_t serial[4];

    if (!initialized) {
        memcpy(serial, &SERIAL, 4);
        initialized = 1;
    }

    return serial;
}
