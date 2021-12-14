#include <stdlib.h>
#include "naomi/maple.h"
#include "naomi/eeprom.h"

void test_eeprom_parser(test_context_t *context)
{
    // Create a new eeprom with a bunch of stuff changed.
    eeprom_t basic;
    memcpy(basic.system.serial, "BTS0", 4);
    basic.system.attract_sounds = ATTRACT_SOUNDS_OFF;
    basic.system.monitor_orientation = MONITOR_ORIENTATION_VERTICAL;
    basic.system.players = 4;
    basic.system.chute_setting = COIN_CHUTE_INDIVIDUAL;
    basic.system.coin_assignment = COIN_ASSIGNMENT_MANUAL;
    basic.system.coins_per_credit = 5;
    basic.system.chute_1_multiplier = 6;
    basic.system.chute_2_multiplier = 7;
    basic.system.bonus_coin = 8;
    basic.system.sequences[0] = 5;
    basic.system.sequences[1] = 4;
    basic.system.sequences[2] = 3;
    basic.system.sequences[3] = 2;
    basic.system.sequences[4] = 2;
    basic.system.sequences[5] = 3;
    basic.system.sequences[6] = 4;
    basic.system.sequences[7] = 5;
    basic.game.size = 10;
    memcpy(basic.game.data, "1234567890", 10);

    // Spit it out as a valid EEPROM file.
    uint8_t eeprom[128];
    unparse_eeprom(eeprom, &basic);

    // Validate all banks.
    ASSERT(eeprom_system_valid(eeprom, EEPROM_BANK_1), "Expected bank 1 of system section to be valid!");
    ASSERT(eeprom_system_valid(eeprom, EEPROM_BANK_2), "Expected bank 2 of system section to be valid!");
    ASSERT(eeprom_game_valid(eeprom, EEPROM_BANK_1), "Expected bank 1 of game section to be valid!");
    ASSERT(eeprom_game_valid(eeprom, EEPROM_BANK_2), "Expected bank 2 of game section to be valid!");

    // Now, mess with a byte in each section and verify that we still load
    // the EEPROM successfully.
    eeprom[SYSTEM_CHUNK_1 + 7] ^= 0xAA;
    ASSERT(!eeprom_system_valid(eeprom, EEPROM_BANK_1), "Expected bank 1 of system section to be invalid!");
    ASSERT(eeprom_system_valid(eeprom, EEPROM_BANK_2), "Expected bank 2 of system section to be valid!");
    ASSERT(eeprom_game_valid(eeprom, EEPROM_BANK_1), "Expected bank 1 of game section to be valid!");
    ASSERT(eeprom_game_valid(eeprom, EEPROM_BANK_2), "Expected bank 2 of game section to be valid!");

    eeprom_t new;
    memset(&new, 0, sizeof(eeprom_t));
    parse_eeprom(eeprom, &new);
    ASSERT(memcmp(new.system.serial, basic.system.serial, 4) == 0, "System EEPROM does not match expected ROM header EEPROM!");
    ASSERT(new.system.attract_sounds == basic.system.attract_sounds, "EEPROM contents does not match!");
    ASSERT(new.system.monitor_orientation == basic.system.monitor_orientation, "EEPROM contents does not match!");
    ASSERT(new.system.players == basic.system.players, "EEPROM contents does not match!");
    ASSERT(new.system.chute_setting == basic.system.chute_setting, "EEPROM contents does not match!");
    ASSERT(new.system.coin_assignment == basic.system.coin_assignment, "EEPROM contents does not match!");
    ASSERT(new.system.coins_per_credit == basic.system.coins_per_credit, "EEPROM contents does not match!");
    ASSERT(new.system.chute_1_multiplier == basic.system.chute_1_multiplier, "EEPROM contents does not match!");
    ASSERT(new.system.chute_2_multiplier == basic.system.chute_2_multiplier, "EEPROM contents does not match!");
    ASSERT(new.system.bonus_coin == basic.system.bonus_coin, "EEPROM contents does not match!");
    ASSERT(new.system.sequences[0] == basic.system.sequences[0], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[1] == basic.system.sequences[1], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[2] == basic.system.sequences[2], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[3] == basic.system.sequences[3], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[4] == basic.system.sequences[4], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[5] == basic.system.sequences[5], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[6] == basic.system.sequences[6], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[7] == basic.system.sequences[7], "EEPROM contents does not match!");
    ASSERT(new.game.size == basic.game.size, "EEPROM contents does not match!");
    ASSERT(memcmp(new.game.data, basic.game.data, basic.game.size) == 0, "System EEPROM does not match expected ROM header EEPROM!");

    eeprom[SYSTEM_CHUNK_1 + 7] ^= 0xAA;
    eeprom[SYSTEM_CHUNK_2 + 7] ^= 0xAA;
    ASSERT(eeprom_system_valid(eeprom, EEPROM_BANK_1), "Expected bank 1 of system section to be valid!");
    ASSERT(!eeprom_system_valid(eeprom, EEPROM_BANK_2), "Expected bank 2 of system section to be invalid!");
    ASSERT(eeprom_game_valid(eeprom, EEPROM_BANK_1), "Expected bank 1 of game section to be valid!");
    ASSERT(eeprom_game_valid(eeprom, EEPROM_BANK_2), "Expected bank 2 of game section to be valid!");

    memset(&new, 0, sizeof(eeprom_t));
    parse_eeprom(eeprom, &new);
    ASSERT(memcmp(new.system.serial, basic.system.serial, 4) == 0, "System EEPROM does not match expected ROM header EEPROM!");
    ASSERT(new.system.attract_sounds == basic.system.attract_sounds, "EEPROM contents does not match!");
    ASSERT(new.system.monitor_orientation == basic.system.monitor_orientation, "EEPROM contents does not match!");
    ASSERT(new.system.players == basic.system.players, "EEPROM contents does not match!");
    ASSERT(new.system.chute_setting == basic.system.chute_setting, "EEPROM contents does not match!");
    ASSERT(new.system.coin_assignment == basic.system.coin_assignment, "EEPROM contents does not match!");
    ASSERT(new.system.coins_per_credit == basic.system.coins_per_credit, "EEPROM contents does not match!");
    ASSERT(new.system.chute_1_multiplier == basic.system.chute_1_multiplier, "EEPROM contents does not match!");
    ASSERT(new.system.chute_2_multiplier == basic.system.chute_2_multiplier, "EEPROM contents does not match!");
    ASSERT(new.system.bonus_coin == basic.system.bonus_coin, "EEPROM contents does not match!");
    ASSERT(new.system.sequences[0] == basic.system.sequences[0], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[1] == basic.system.sequences[1], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[2] == basic.system.sequences[2], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[3] == basic.system.sequences[3], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[4] == basic.system.sequences[4], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[5] == basic.system.sequences[5], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[6] == basic.system.sequences[6], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[7] == basic.system.sequences[7], "EEPROM contents does not match!");
    ASSERT(new.game.size == basic.game.size, "EEPROM contents does not match!");
    ASSERT(memcmp(new.game.data, basic.game.data, basic.game.size) == 0, "System EEPROM does not match expected ROM header EEPROM!");

    eeprom[SYSTEM_CHUNK_2 + 7] ^= 0xAA;
    eeprom[GAME_PAYLOAD + 7] ^= 0xAA;
    ASSERT(eeprom_system_valid(eeprom, EEPROM_BANK_1), "Expected bank 1 of system section to be valid!");
    ASSERT(eeprom_system_valid(eeprom, EEPROM_BANK_2), "Expected bank 2 of system section to be valid!");
    ASSERT(!eeprom_game_valid(eeprom, EEPROM_BANK_1), "Expected bank 1 of game section to be invalid!");
    ASSERT(eeprom_game_valid(eeprom, EEPROM_BANK_2), "Expected bank 2 of game section to be valid!");

    memset(&new, 0, sizeof(eeprom_t));
    parse_eeprom(eeprom, &new);
    ASSERT(memcmp(new.system.serial, basic.system.serial, 4) == 0, "System EEPROM does not match expected ROM header EEPROM!");
    ASSERT(new.system.attract_sounds == basic.system.attract_sounds, "EEPROM contents does not match!");
    ASSERT(new.system.monitor_orientation == basic.system.monitor_orientation, "EEPROM contents does not match!");
    ASSERT(new.system.players == basic.system.players, "EEPROM contents does not match!");
    ASSERT(new.system.chute_setting == basic.system.chute_setting, "EEPROM contents does not match!");
    ASSERT(new.system.coin_assignment == basic.system.coin_assignment, "EEPROM contents does not match!");
    ASSERT(new.system.coins_per_credit == basic.system.coins_per_credit, "EEPROM contents does not match!");
    ASSERT(new.system.chute_1_multiplier == basic.system.chute_1_multiplier, "EEPROM contents does not match!");
    ASSERT(new.system.chute_2_multiplier == basic.system.chute_2_multiplier, "EEPROM contents does not match!");
    ASSERT(new.system.bonus_coin == basic.system.bonus_coin, "EEPROM contents does not match!");
    ASSERT(new.system.sequences[0] == basic.system.sequences[0], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[1] == basic.system.sequences[1], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[2] == basic.system.sequences[2], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[3] == basic.system.sequences[3], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[4] == basic.system.sequences[4], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[5] == basic.system.sequences[5], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[6] == basic.system.sequences[6], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[7] == basic.system.sequences[7], "EEPROM contents does not match!");
    ASSERT(new.game.size == basic.game.size, "EEPROM contents does not match!");
    ASSERT(memcmp(new.game.data, basic.game.data, basic.game.size) == 0, "System EEPROM does not match expected ROM header EEPROM!");

    eeprom[GAME_PAYLOAD + 7] ^= 0xAA;
    eeprom[GAME_PAYLOAD + 17] ^= 0xAA;
    ASSERT(eeprom_system_valid(eeprom, EEPROM_BANK_1), "Expected bank 1 of system section to be valid!");
    ASSERT(eeprom_system_valid(eeprom, EEPROM_BANK_2), "Expected bank 2 of system section to be valid!");
    ASSERT(eeprom_game_valid(eeprom, EEPROM_BANK_1), "Expected bank 1 of game section to be valid!");
    ASSERT(!eeprom_game_valid(eeprom, EEPROM_BANK_2), "Expected bank 2 of game section to be invalid!");

    memset(&new, 0, sizeof(eeprom_t));
    parse_eeprom(eeprom, &new);
    ASSERT(memcmp(new.system.serial, basic.system.serial, 4) == 0, "System EEPROM does not match expected ROM header EEPROM!");
    ASSERT(new.system.attract_sounds == basic.system.attract_sounds, "EEPROM contents does not match!");
    ASSERT(new.system.monitor_orientation == basic.system.monitor_orientation, "EEPROM contents does not match!");
    ASSERT(new.system.players == basic.system.players, "EEPROM contents does not match!");
    ASSERT(new.system.chute_setting == basic.system.chute_setting, "EEPROM contents does not match!");
    ASSERT(new.system.coin_assignment == basic.system.coin_assignment, "EEPROM contents does not match!");
    ASSERT(new.system.coins_per_credit == basic.system.coins_per_credit, "EEPROM contents does not match!");
    ASSERT(new.system.chute_1_multiplier == basic.system.chute_1_multiplier, "EEPROM contents does not match!");
    ASSERT(new.system.chute_2_multiplier == basic.system.chute_2_multiplier, "EEPROM contents does not match!");
    ASSERT(new.system.bonus_coin == basic.system.bonus_coin, "EEPROM contents does not match!");
    ASSERT(new.system.sequences[0] == basic.system.sequences[0], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[1] == basic.system.sequences[1], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[2] == basic.system.sequences[2], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[3] == basic.system.sequences[3], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[4] == basic.system.sequences[4], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[5] == basic.system.sequences[5], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[6] == basic.system.sequences[6], "EEPROM contents does not match!");
    ASSERT(new.system.sequences[7] == basic.system.sequences[7], "EEPROM contents does not match!");
    ASSERT(new.game.size == basic.game.size, "EEPROM contents does not match!");
    ASSERT(memcmp(new.game.data, basic.game.data, basic.game.size) == 0, "System EEPROM does not match expected ROM header EEPROM!");
}

void test_eeprom_hardware(test_context_t *context)
{
    // Read from the BIOS's initialization of us.
    eeprom_t original;
    ASSERT(eeprom_read(&original) == 0, "Failed to read system EEPROM!");
    ASSERT(memcmp(original.system.serial, eeprom_serial(), 4) == 0, "System EEPROM does not match expected ROM header EEPROM!");

    // Create a new eeprom with a bunch of stuff changed.
    eeprom_t update;
    memcpy(update.system.serial, "BTS0", 4);
    update.system.attract_sounds = ATTRACT_SOUNDS_OFF;
    update.system.monitor_orientation = MONITOR_ORIENTATION_VERTICAL;
    update.system.players = 4;
    update.system.chute_setting = COIN_CHUTE_INDIVIDUAL;
    update.system.coin_assignment = COIN_ASSIGNMENT_MANUAL;
    update.system.coins_per_credit = 5;
    update.system.chute_1_multiplier = 6;
    update.system.chute_2_multiplier = 7;
    update.system.bonus_coin = 8;
    update.system.sequences[0] = 5;
    update.system.sequences[1] = 4;
    update.system.sequences[2] = 3;
    update.system.sequences[3] = 2;
    update.system.sequences[4] = 2;
    update.system.sequences[5] = 3;
    update.system.sequences[6] = 4;
    update.system.sequences[7] = 5;
    update.game.size = 10;
    memcpy(update.game.data, "1234567890", 10);

    // Write this new EEPROM to the system eeprom.
    ASSERT(eeprom_write(&update) == 0, "Failed to write system EEPROM!");

    // Now read it back and verify everything is correct.
    eeprom_t readback;
    ASSERT(eeprom_read(&readback) == 0, "Failed to read system EEPROM!");
    ASSERT(memcmp(readback.system.serial, update.system.serial, 4) == 0, "System EEPROM does not match expected ROM header EEPROM!");
    ASSERT(readback.system.attract_sounds == update.system.attract_sounds, "EEPROM contents does not match!");
    ASSERT(readback.system.monitor_orientation == update.system.monitor_orientation, "EEPROM contents does not match!");
    ASSERT(readback.system.players == update.system.players, "EEPROM contents does not match!");
    ASSERT(readback.system.chute_setting == update.system.chute_setting, "EEPROM contents does not match!");
    ASSERT(readback.system.coin_assignment == update.system.coin_assignment, "EEPROM contents does not match!");
    ASSERT(readback.system.coins_per_credit == update.system.coins_per_credit, "EEPROM contents does not match!");
    ASSERT(readback.system.chute_1_multiplier == update.system.chute_1_multiplier, "EEPROM contents does not match!");
    ASSERT(readback.system.chute_2_multiplier == update.system.chute_2_multiplier, "EEPROM contents does not match!");
    ASSERT(readback.system.bonus_coin == update.system.bonus_coin, "EEPROM contents does not match!");
    ASSERT(readback.system.sequences[0] == update.system.sequences[0], "EEPROM contents does not match!");
    ASSERT(readback.system.sequences[1] == update.system.sequences[1], "EEPROM contents does not match!");
    ASSERT(readback.system.sequences[2] == update.system.sequences[2], "EEPROM contents does not match!");
    ASSERT(readback.system.sequences[3] == update.system.sequences[3], "EEPROM contents does not match!");
    ASSERT(readback.system.sequences[4] == update.system.sequences[4], "EEPROM contents does not match!");
    ASSERT(readback.system.sequences[5] == update.system.sequences[5], "EEPROM contents does not match!");
    ASSERT(readback.system.sequences[6] == update.system.sequences[6], "EEPROM contents does not match!");
    ASSERT(readback.system.sequences[7] == update.system.sequences[7], "EEPROM contents does not match!");
    ASSERT(readback.game.size == update.game.size, "EEPROM contents does not match!");
    ASSERT(memcmp(readback.game.data, update.game.data, update.game.size) == 0, "System EEPROM does not match expected ROM header EEPROM!");

    // Now, clear the EEPROM and verify that we get defaults back.
    uint8_t clear_buf[128];
    memset(clear_buf, 0xFF, 128);
    ASSERT(maple_request_eeprom_write(clear_buf) == 0, "Could not clear system EEPROM!");

    ASSERT(eeprom_read(&readback) == 0, "Failed to read system EEPROM!");
    ASSERT(memcmp(readback.system.serial, eeprom_serial(), 4) == 0, "System EEPROM does not match expected ROM header EEPROM!");
    ASSERT(readback.game.size == 0, "EEPROM defaults wrong!");

    // Now, write back the original to restore the data.
    ASSERT(eeprom_write(&original) == 0, "Failed to write system EEPROM!");

    // Finally, make sure it is actually good.
    ASSERT(eeprom_read(&readback) == 0, "Failed to read system EEPROM!");
    ASSERT(memcmp(readback.system.serial, original.system.serial, 4) == 0, "System EEPROM does not match expected ROM header EEPROM!");
    ASSERT(readback.system.attract_sounds == original.system.attract_sounds, "EEPROM contents does not match!");
    ASSERT(readback.system.monitor_orientation == original.system.monitor_orientation, "EEPROM contents does not match!");
    ASSERT(readback.system.players == original.system.players, "EEPROM contents does not match!");
    ASSERT(readback.system.chute_setting == original.system.chute_setting, "EEPROM contents does not match!");
    ASSERT(readback.system.coin_assignment == original.system.coin_assignment, "EEPROM contents does not match!");
    ASSERT(readback.system.coins_per_credit == original.system.coins_per_credit, "EEPROM contents does not match!");
    ASSERT(readback.system.chute_1_multiplier == original.system.chute_1_multiplier, "EEPROM contents does not match!");
    ASSERT(readback.system.chute_2_multiplier == original.system.chute_2_multiplier, "EEPROM contents does not match!");
    ASSERT(readback.system.bonus_coin == original.system.bonus_coin, "EEPROM contents does not match!");
    ASSERT(readback.system.sequences[0] == original.system.sequences[0], "EEPROM contents does not match!");
    ASSERT(readback.system.sequences[1] == original.system.sequences[1], "EEPROM contents does not match!");
    ASSERT(readback.system.sequences[2] == original.system.sequences[2], "EEPROM contents does not match!");
    ASSERT(readback.system.sequences[3] == original.system.sequences[3], "EEPROM contents does not match!");
    ASSERT(readback.system.sequences[4] == original.system.sequences[4], "EEPROM contents does not match!");
    ASSERT(readback.system.sequences[5] == original.system.sequences[5], "EEPROM contents does not match!");
    ASSERT(readback.system.sequences[6] == original.system.sequences[6], "EEPROM contents does not match!");
    ASSERT(readback.system.sequences[7] == original.system.sequences[7], "EEPROM contents does not match!");
    ASSERT(readback.game.size == original.game.size, "EEPROM contents does not match!");
    ASSERT(memcmp(readback.game.data, original.game.data, original.game.size) == 0, "System EEPROM does not match expected ROM header EEPROM!");
}
