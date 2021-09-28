#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "naomi/video.h"
#include "naomi/maple.h"

// We will overwrite this in the final linking script when we are injected
// into a binary. It will point to the original entrypoint that was in the
// binary's header.
uint32_t settings_chunk[6] = {
    0xEEEEEEEE,
    0xAAAAAAAA,
    START_ADDR,
    0xCFCFCFCF,  // Enable sentinel behavior
    0xDDDDDDDD,  // Enable debug printing
    0xEEEEEEEE,
};

#define GAME_ENTRYPOINT 1
#define OUT_ENTRYPOINT 2
#define SENTINEL_ENABLED 3
#define DEBUG_ENABLED 4

// We will overwrite this as well when we link. It will contain the EEPROM
// contents that we wish to write.
uint8_t requested_eeprom[128] = {
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
};

// We will write to this once we've initialized the EEPROM, so we don't
// mess with settings after somebody goes into the test menu after loading us.
uint32_t *sentinel = (uint32_t *)(START_ADDR - 4);

// Location of the text for debugging.
#define X_LOC 200
#define Y_LOC 200

// Wait time to display debugging when enabled.
#define WAIT_TIME_NORMAL 5
#define WAIT_TIME_DEBUG 15

// Whether to display verbose debugging info when debug printing is enabled.
#define VERBOSE_DEBUG_MODE 0

// Location of the two system data chunks inside the EEPROM.
#define SYSTEM_CHUNK_1 0
#define SYSTEM_CHUNK_2 18

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
#define GAME_LEN_LOC 2
#define GAME_LEN_SIZE 1

uint32_t crc_inner(uint32_t running_crc, uint8_t next_byte)
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

uint16_t crc(uint8_t *data, unsigned int len)
{
    uint32_t running_crc = 0xDEBDEB00;

    // CRC over all the data we've been given.
    for (unsigned int i = 0; i < len; i++)
    {
        running_crc = crc_inner(running_crc, data[i]);
    }

    // Add in the null byte that Naomi BIOS seems to want.
    running_crc = crc_inner(running_crc, 0);

    // Calculate the final CRC value by taking the remainder.
    return (running_crc >> 16) & 0xFFFF;
}

int eeprom_valid(uint8_t *eeprom)
{
    uint16_t expected = 0;

    // Calculate first system chunk.
    memcpy(&expected, eeprom + SYSTEM_CHUNK_1 + SYSTEM_CRC_LOC, SYSTEM_CRC_SIZE);
    if (expected != crc(eeprom + SYSTEM_CHUNK_1 + SYSTEM_CRC_REGION_LOC, SYSTEM_CRC_REGION_SIZE))
    {
        return 0;
    }

    // Calculate second system chunk.
    memcpy(&expected, eeprom + SYSTEM_CHUNK_2 + SYSTEM_CRC_LOC, SYSTEM_CRC_SIZE);
    if (expected != crc(eeprom + SYSTEM_CHUNK_2 + SYSTEM_CRC_REGION_LOC, SYSTEM_CRC_REGION_SIZE))
    {
        return 0;
    }

    // Calculate first game chunk.
    memcpy(&expected, eeprom + GAME_CHUNK_1 + GAME_CRC_LOC, GAME_CRC_SIZE);
    if (expected != crc(eeprom + GAME_PAYLOAD, eeprom[GAME_CHUNK_1 + GAME_LEN_LOC]))
    {
        return 0;
    }

    // Calculate second game chunk.
    memcpy(&expected, eeprom + GAME_CHUNK_2 + GAME_CRC_LOC, GAME_CRC_SIZE);
    if (expected != crc(eeprom + GAME_PAYLOAD + eeprom[GAME_CHUNK_1 + GAME_LEN_LOC], eeprom[GAME_CHUNK_2 + GAME_LEN_LOC]))
    {
        return 0;
    }

    // All 4 CRCs passed!
    return 1;
}

uint32_t get_sentinel()
{
    uint16_t sys_crc;
    uint16_t game_crc;

    memcpy(&sys_crc, requested_eeprom + SYSTEM_CHUNK_1 + SYSTEM_CRC_LOC, SYSTEM_CRC_SIZE);
    memcpy(&game_crc, requested_eeprom + GAME_CHUNK_1 + GAME_CRC_LOC, GAME_CRC_SIZE);

    // We base the sentinel on the system and game CRC for the requested EEPROM, that way
    // if the user attaches a new settings file to a game and tries to netboot it, we will
    // know that we should wipe the old settings and re-load our settings.
    return ((uint32_t)sys_crc) << 16 | game_crc;
}

void main()
{
    // Set up a crude console
    video_init_simple();
    maple_init();

    video_fill_screen(rgb(0, 0, 0));
    video_draw_text(X_LOC, Y_LOC, rgb(255, 255, 255), "Checking settings...");

    // Read current EEPROM contents
    uint8_t current_eeprom[128];
    memset(current_eeprom, 0, 128);

    // First, try to read, bail out of it fails. If we send a new game over and an old
    // one was running, the sentinel might still be written from last time, so we need
    // to ensure that the system settings match the current game.
    if(maple_request_eeprom_read(current_eeprom) == 0)
    {
        // Save the old sentinel for debugging.
        uint32_t old_sentinel = *sentinel;

        // Compare the requested and current eeprom game serial.
        uint32_t requested_game;
        memcpy(&requested_game, requested_eeprom + SYSTEM_CHUNK_1 + SYSTEM_SERIAL_LOC, SYSTEM_SERIAL_SIZE);

        uint32_t current_game;
        memcpy(&current_game, current_eeprom + SYSTEM_CHUNK_1 + SYSTEM_SERIAL_LOC, SYSTEM_SERIAL_SIZE);

        // We check a few things here before deciding to write. First, we make sure that
        // the sentinel we wrote previously matches. If it does not, that means that
        // somebody netbooted the same game with a new settings file. Second, we make
        // sure that the game serial for the EEPROM we're going to write matches the
        // system EEPROM. If not, that means we need to write (although we should never
        // get into this state). Third, we make sure that system AND game settings CRCs
        // are correct in the current EEPROM. If not, that means the Naomi defaulted us
        // to a new EEPROM after we switched games via netboot and we need to write our
        // settings. Finally, we check the lengths of the game settings section, as a last
        // ditch effort to catch if a previous game had valid settings and when we switched
        // games the system didn't wipe the settings.
        uint32_t expected_sentinel = get_sentinel();
        if (
            (settings_chunk[SENTINEL_ENABLED] != 0 && expected_sentinel != *sentinel) ||
            current_game != requested_game ||
            current_eeprom[GAME_CHUNK_1 + GAME_LEN_LOC] != requested_eeprom[GAME_CHUNK_1 + GAME_LEN_LOC] ||
            eeprom_valid(current_eeprom) == 0
        )
        {
            video_draw_text(X_LOC, Y_LOC + 12, rgb(255, 255, 255), "Settings need to be written...");

            if(maple_request_eeprom_write(requested_eeprom) == 0)
            {
                video_draw_text(X_LOC, Y_LOC + 24, rgb(0, 255, 0), "Success, your settings are written!");

                if (settings_chunk[SENTINEL_ENABLED] != 0)
                {
                    // Make sure we don't do this again this power cycle. This lets us
                    // go into the test menu and change settings.
                    *sentinel = get_sentinel();
                }
            }
            else
            {
                video_draw_text(X_LOC, Y_LOC + 24, rgb(255, 0, 0), "Failed, could not write your settings!");
            }
        }
        else
        {
            video_draw_text(X_LOC, Y_LOC + 12, rgb(255, 255, 255), "Settings have already been written!");
        }

        if (VERBOSE_DEBUG_MODE)
        {
            // Debug print the current EEPROM contents.
            char eeprom_buf[512];
            memset(eeprom_buf, 0, 512);
            for(int i = 0; i < 128; i++)
            {
                sprintf(
                    eeprom_buf + strlen(eeprom_buf),
                    "%02X ",
                    current_eeprom[i]
                );

                if(i % 16 == 7)
                {
                    strcat(eeprom_buf, "  ");
                }
                else if(i % 16 == 15)
                {
                    strcat(eeprom_buf, "\n");
                }
            }

            video_draw_text(140, Y_LOC - (8 * 10), rgb(255, 255, 64), eeprom_buf);

            // Debug print the current EEPROM CRC values.
            uint16_t expected = 0;
            memset(eeprom_buf, 0, 512);

            // Calculate and display first system chunk.
            memcpy(&expected, current_eeprom + SYSTEM_CHUNK_1 + SYSTEM_CRC_LOC, SYSTEM_CRC_SIZE);
            sprintf(
                eeprom_buf + strlen(eeprom_buf),
                "Sys Chunk 1 Expected: %04X Calc:%04X\n",
                expected,
                crc(current_eeprom + SYSTEM_CHUNK_1 + SYSTEM_CRC_REGION_LOC, SYSTEM_CRC_REGION_SIZE)
            );

            // Calculate and display second system chunk.
            memcpy(&expected, current_eeprom + SYSTEM_CHUNK_2 + SYSTEM_CRC_LOC, SYSTEM_CRC_SIZE);
            sprintf(
                eeprom_buf + strlen(eeprom_buf),
                "Sys Chunk 2 Expected: %04X Calc:%04X\n",
                expected,
                crc(current_eeprom + SYSTEM_CHUNK_2 + SYSTEM_CRC_REGION_LOC, SYSTEM_CRC_REGION_SIZE)
            );

            // Calculate and display first game chunk.
            memcpy(&expected, current_eeprom + GAME_CHUNK_1 + GAME_CRC_LOC, GAME_CRC_SIZE);
            sprintf(
                eeprom_buf + strlen(eeprom_buf),
                "Game Chunk 1 Expected: %04X Calc:%04X\n",
                expected,
                crc(current_eeprom + GAME_PAYLOAD, current_eeprom[GAME_CHUNK_1 + GAME_LEN_LOC])
            );

            // Calculate and display second game chunk.
            memcpy(&expected, current_eeprom + GAME_CHUNK_2 + GAME_CRC_LOC, GAME_CRC_SIZE);
            sprintf(
                eeprom_buf + strlen(eeprom_buf),
                "Game Chunk 2 Expected: %04X Calc:%04X\n",
                expected,
                crc(current_eeprom + GAME_PAYLOAD + current_eeprom[GAME_CHUNK_1 + GAME_LEN_LOC], current_eeprom[GAME_CHUNK_2 + GAME_LEN_LOC])
            );

            sprintf(
                eeprom_buf + strlen(eeprom_buf),
                "Length expected: %d Current: %d\n",
                requested_eeprom[GAME_CHUNK_1 + GAME_LEN_LOC],
                current_eeprom[GAME_CHUNK_1 + GAME_LEN_LOC]
            );

            if (settings_chunk[SENTINEL_ENABLED] != 0)
            {
                // Calculate the sentinel.
                sprintf(
                    eeprom_buf + strlen(eeprom_buf),
                    "Sentinel expected: %04X Calc: %04X\n",
                    old_sentinel,
                    get_sentinel()
                );
            }

            video_draw_text(X_LOC, Y_LOC + 36, rgb(255, 255, 255), eeprom_buf);
        }
    }
    else
    {
        video_draw_text(X_LOC, Y_LOC + 12, rgb(255, 0, 0), "Failed, could not read current settings!");
    }

    if (settings_chunk[DEBUG_ENABLED] != 0)
    {
        video_wait_for_vblank();
        video_display();

        // Wait some seconds to display debugging.
        for (int i = 0; i < 60 * (VERBOSE_DEBUG_MODE ? WAIT_TIME_DEBUG : WAIT_TIME_NORMAL); i++) {
            video_wait_for_vblank();
        }
    }

    // Boot original code.
    void (*jump_to_exe)() = (void (*))settings_chunk[GAME_ENTRYPOINT];
    jump_to_exe();
}

void test()
{
    // Just a stub in order to compile, we should never call this function since
    // we are never going to overwrite test mode.
}
