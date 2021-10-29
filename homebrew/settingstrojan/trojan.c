#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "naomi/video.h"
#include "naomi/maple.h"
#include "naomi/eeprom.h"

// We will overwrite this in the final linking script when we are injected
// into a binary. It will point to the original entrypoint that was in the
// binary's header.
uint32_t settings_chunk[7] = {
    0xEEEEEEEE,
    0xAAAAAAAA,
    START_ADDR,
    0xCFCFCFCF,  // Reserved for future use
    0xDDDDDDDD,  // Enable debug printing
    BUILD_DATE,  // Version of this trojan as a date in YYYYMMDD format.
    0xEEEEEEEE,
};

#define GAME_ENTRYPOINT 1
#define OUT_ENTRYPOINT 2
#define RESERVED_FUTURE_USE 3
#define DEBUG_ENABLED 4
#define VERSION 5

// We will overwrite this as well when we link. It will contain the EEPROM
// contents that we wish to write.
uint8_t requested_eeprom[EEPROM_SIZE] = {
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

// Location of the text for debugging.
#define X_LOC 200
#define Y_LOC 200

// Wait time to display debugging when enabled.
#define WAIT_TIME_NORMAL 5
#define WAIT_TIME_DEBUG 15

// Whether to display verbose debugging info when debug printing is enabled.
#define VERBOSE_DEBUG_MODE 0

void main()
{
    // Set up a crude console
    video_init_simple();

    video_fill_screen(rgb(0, 0, 0));
    video_draw_debug_text(X_LOC, Y_LOC, rgb(255, 255, 255), "Checking settings...");

    // First, try to read, bail out of it fails.
    uint8_t current_eeprom[EEPROM_SIZE];
    if(maple_request_eeprom_read(current_eeprom) == 0)
    {
        // Initialize each section of the EEPROM based on whether we have a valid
        // copy of it ourselves.
        int initialized = 0;
        if (memcmp(current_eeprom, requested_eeprom, EEPROM_SIZE) != 0)
        {
            if (eeprom_system_valid(requested_eeprom))
            {
                memcpy(&current_eeprom[SYSTEM_SECTION], &requested_eeprom[SYSTEM_SECTION], SYSTEM_LENGTH);
                initialized |= 1;
            }
            if (eeprom_game_valid(requested_eeprom))
            {
                memcpy(&current_eeprom[GAME_SECTION], &requested_eeprom[GAME_SECTION], GAME_LENGTH);
                initialized |= 2;
            }
        }

        if (initialized != 0)
        {
            video_draw_debug_text(X_LOC, Y_LOC + 12, rgb(255, 255, 255), "Settings need to be written...");

            if(maple_request_eeprom_write(current_eeprom) == 0)
            {
                video_draw_debug_text(X_LOC, Y_LOC + 24, rgb(0, 255, 0), "Success, your settings are written!");
            }
            else
            {
                video_draw_debug_text(X_LOC, Y_LOC + 24, rgb(255, 0, 0), "Failed, could not write your settings!");
            }
        }
        else
        {
            video_draw_debug_text(X_LOC, Y_LOC + 12, rgb(255, 255, 255), "Settings have already been written!");
        }

        if (VERBOSE_DEBUG_MODE)
        {
            // Debug print the current EEPROM contents.
            char eeprom_buf[512];
            memset(eeprom_buf, 0, 512);
            for(int i = 0; i < EEPROM_SIZE; i++)
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

            video_draw_debug_text(140, Y_LOC - (8 * 10), rgb(255, 255, 64), eeprom_buf);

            // Debug print the current EEPROM CRC values.
            uint16_t expected = 0;
            memset(eeprom_buf, 0, 512);

            // Display what we wrote.
            if (initialized & 1)
            {
                sprintf(
                    eeprom_buf + strlen(eeprom_buf),
                    "System settings initialized!\n"
                );
            }
            if (initialized & 2)
            {
                sprintf(
                    eeprom_buf + strlen(eeprom_buf),
                    "Game settings initialized!\n"
                );
            }

            // Calculate and display first system chunk.
            memcpy(&expected, current_eeprom + SYSTEM_CHUNK_1 + SYSTEM_CRC_LOC, SYSTEM_CRC_SIZE);
            sprintf(
                eeprom_buf + strlen(eeprom_buf),
                "Sys Chunk 1 Expected: %04X Calc:%04X\n",
                expected,
                eeprom_crc(current_eeprom + SYSTEM_CHUNK_1 + SYSTEM_CRC_REGION_LOC, SYSTEM_CRC_REGION_SIZE)
            );

            // Calculate and display second system chunk.
            memcpy(&expected, current_eeprom + SYSTEM_CHUNK_2 + SYSTEM_CRC_LOC, SYSTEM_CRC_SIZE);
            sprintf(
                eeprom_buf + strlen(eeprom_buf),
                "Sys Chunk 2 Expected: %04X Calc:%04X\n",
                expected,
                eeprom_crc(current_eeprom + SYSTEM_CHUNK_2 + SYSTEM_CRC_REGION_LOC, SYSTEM_CRC_REGION_SIZE)
            );

            if (current_eeprom[GAME_CHUNK_1 + GAME_LEN_LOC_1] != 0xFF && requested_eeprom[GAME_CHUNK_1 + GAME_LEN_LOC_1] != 0xFF)
            {
                // Calculate and display first game chunk.
                memcpy(&expected, current_eeprom + GAME_CHUNK_1 + GAME_CRC_LOC, GAME_CRC_SIZE);
                sprintf(
                    eeprom_buf + strlen(eeprom_buf),
                    "Game Chunk 1 Expected: %04X Calc:%04X\n",
                    expected,
                    eeprom_crc(current_eeprom + GAME_PAYLOAD, current_eeprom[GAME_CHUNK_1 + GAME_LEN_LOC_1])
                );
            }

            if (current_eeprom[GAME_CHUNK_2 + GAME_LEN_LOC_1] != 0xFF && requested_eeprom[GAME_CHUNK_2 + GAME_LEN_LOC_1] != 0xFF)
            {
                // Calculate and display second game chunk.
                memcpy(&expected, current_eeprom + GAME_CHUNK_2 + GAME_CRC_LOC, GAME_CRC_SIZE);
                sprintf(
                    eeprom_buf + strlen(eeprom_buf),
                    "Game Chunk 2 Expected: %04X Calc:%04X\n",
                    expected,
                    eeprom_crc(current_eeprom + GAME_PAYLOAD + current_eeprom[GAME_CHUNK_1 + GAME_LEN_LOC_1], current_eeprom[GAME_CHUNK_2 + GAME_LEN_LOC_1])
                );
            }

            sprintf(
                eeprom_buf + strlen(eeprom_buf),
                "Length expected: %d Current: %d\n",
                requested_eeprom[GAME_CHUNK_1 + GAME_LEN_LOC_1],
                current_eeprom[GAME_CHUNK_1 + GAME_LEN_LOC_1]
            );

            video_draw_debug_text(X_LOC, Y_LOC + 36, rgb(255, 255, 255), eeprom_buf);
        }
    }
    else
    {
        video_draw_debug_text(X_LOC, Y_LOC + 12, rgb(255, 0, 0), "Failed, could not read current settings!");
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
