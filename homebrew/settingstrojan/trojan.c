#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/reent.h>
#include <sys/errno.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "video.h"
#include "eeprom.h"

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
    if (settings_chunk[DEBUG_ENABLED] != 0)
    {
        // Set up a crude console
        video_init_simple();

        video_fill_screen(rgb(0, 0, 0));
        video_draw_debug_text(X_LOC, Y_LOC, rgb(255, 255, 255), "Checking settings...");
    }

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
            if (settings_chunk[DEBUG_ENABLED] != 0)
            {
                video_draw_debug_text(X_LOC, Y_LOC + 12, rgb(255, 255, 255), "Settings need to be written...");
            }

            if(maple_request_eeprom_write(current_eeprom) == 0)
            {
                if (settings_chunk[DEBUG_ENABLED] != 0)
                {
                    video_draw_debug_text(X_LOC, Y_LOC + 24, rgb(0, 255, 0), "Success, your settings are written!");
                }
            }
            else
            {
                if (settings_chunk[DEBUG_ENABLED] != 0)
                {
                    video_draw_debug_text(X_LOC, Y_LOC + 24, rgb(255, 0, 0), "Failed, could not write your settings!");
                }
            }
        }
        else
        {
            if (settings_chunk[DEBUG_ENABLED] != 0)
            {
                video_draw_debug_text(X_LOC, Y_LOC + 12, rgb(255, 255, 255), "Settings have already been written!");
            }
        }

        if (settings_chunk[DEBUG_ENABLED] != 0)
        {
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
    }
    else
    {
        if (settings_chunk[DEBUG_ENABLED] != 0)
        {
            video_draw_debug_text(X_LOC, Y_LOC + 12, rgb(255, 0, 0), "Failed, could not read current settings!");
        }
    }

    if (settings_chunk[DEBUG_ENABLED] != 0)
    {
        video_display_on_vblank();

        // Wait some seconds to display debugging.
        for (int i = 0; i < 60 * (VERBOSE_DEBUG_MODE ? WAIT_TIME_DEBUG : WAIT_TIME_NORMAL); i++)
        {
            video_wait_for_vblank();
        }

        video_fill_screen(rgb(0, 0, 0));
        video_display_on_vblank();

        video_free();
    }
}

void _exit(int status)
{
    // Go to original executable.
    void (*jump_to_exe)() = (void (*))settings_chunk[GAME_ENTRYPOINT];
    jump_to_exe();
}

void _enter()
{
    // Set up system DMA to allow for things like Maple to operate. This
    // was kindly copied from the Mvc2 init code after bisecting to it
    // when determining how to initialize Maple.
    ((uint32_t *)0xFFA00020)[0] = 0;
    ((uint32_t *)0xFFA0002C)[0] = 0x1201;
    ((uint32_t *)0xFFA00040)[0] = 0x8201;
    while(((volatile uint32_t *)0xFFA00040)[0] != 0x8201)
    {
        // Spinloop!
        for(int i = 0; i < 0x10000; i++) { ; }
        ((uint32_t *)0xFFA00040)[0] = 0x8201;
    }

    // Run main.
    maple_init();
    main();
    maple_free();

    _exit(0);
}

void __assert_func(const char * file, int line, const char *func, const char *failedexpr)
{
    // Nothing!
}

_ssize_t _read_r(struct _reent *reent, int file, void *ptr, size_t len)
{
    reent->_errno = ENOTSUP;
    return -1;
}

_off_t _lseek_r(struct _reent *reent, int file, _off_t amount, int dir)
{
    reent->_errno = ENOTSUP;
    return -1;
}

_ssize_t _write_r(struct _reent *reent, int file, const void * ptr, size_t len)
{
    reent->_errno = ENOTSUP;
    return -1;
}

int _close_r(struct _reent *reent, int file)
{
    reent->_errno = ENOTSUP;
    return -1;
}

int _link_r(struct _reent *reent, const char *old, const char *new)
{
    reent->_errno = ENOTSUP;
    return -1;
}

int _rename_r (struct _reent *reent, const char *old, const char *new)
{
    reent->_errno = ENOTSUP;
    return -1;
}

void *_sbrk_r(struct _reent *reent, ptrdiff_t incr)
{
    extern char end;      /* Defined by the linker in naomi.ld */
    static char *heap_end;
    char *prev_heap_end;

    if(heap_end == 0)
    {
        heap_end = &end;
    }
    prev_heap_end = heap_end;

    // This really should be checking for the end of stack, but
    // that only really works in the main thread and that only really
    // makes sense if the stack will never grow larger than after
    // this check. So just use the top of memory.
    if(heap_end + incr > (char *)0x0E000000)
    {
        reent->_errno = ENOMEM;
        return (void *)-1;
    }
    heap_end += incr;
    return prev_heap_end;
}

int _fstat_r(struct _reent *reent, int file, struct stat *st)
{
    reent->_errno = ENOTSUP;
    return -1;
}

int _mkdir_r(struct _reent *reent, const char *path, int flags)
{
    reent->_errno = ENOTSUP;
    return -1;
}

int _open_r(struct _reent *reent, const char *path, int flags, int unk)
{
    reent->_errno = ENOTSUP;
    return -1;
}

int _unlink_r(struct _reent *reent, const char *path)
{
    reent->_errno = ENOTSUP;
    return -1;
}

int _isatty_r(struct _reent *reent, int fd)
{
    if (fd == 0 || fd == 1 || fd == 2)
    {
        return 1;
    }
    else
    {
        reent->_errno = ENOTTY;
        return 0;
    }
}

int _kill_r(struct _reent *reent, int n, int m)
{
    reent->_errno = ENOTSUP;
    return -1;
}

int _getpid_r(struct _reent *reent)
{
    reent->_errno = ENOTSUP;
    return -1;
}

int _stat_r(struct _reent *reent, const char *path, struct stat *st)
{
    reent->_errno = ENOTSUP;
    return -1;
}

int _fork_r(struct _reent *reent)
{
    reent->_errno = ENOTSUP;
    return -1;
}

int _wait_r(struct _reent *reent, int *statusp)
{
    reent->_errno = ENOTSUP;
    return -1;
}

int _execve_r(struct _reent *reent, const char *path, char *const argv[], char *const envp[])
{
    reent->_errno = ENOTSUP;
    return -1;
}

_CLOCK_T_ _times_r(struct _reent *reent, struct tms *tm)
{
    reent->_errno = ENOTSUP;
    return -1;
}

int _gettimeofday_r(struct _reent *reent, struct timeval *tv, void *tz)
{
    reent->_errno = ENOTSUP;
    return -1;
}
