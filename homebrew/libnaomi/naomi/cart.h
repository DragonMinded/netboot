#ifndef __CART_H
#define __CART_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Read len bytes from cartridge offset src into destination address dst.
// Note that dst and src both MUST be aligned to 2 bytes, and len must be
// a multiple of 2.
void cart_read(void *dst, uint32_t src, unsigned int len);

// Write len bytes to cartridge offset dst from source address src. Note
// that dst and src both MUST be aligned to 2 bytes, and len must be a
// multiple of 2.
void cart_write(uint32_t dst, void *src, unsigned int len);

// The size of the ROM header.
#define HEADER_SIZE 0x500

// Read the ROM header from cartridge, returning the first HEADER_SIZE bytes
// into a buffer pointed at by dst.
void cart_read_rom_header(void *dst);

typedef struct
{
    // The offset into the ROM this data section lies.
    uint32_t offset;
    // The length of data in bytes at the above ROM offset.
    uint32_t length;
    // Where in system RAM this section gets loaded by the BIOS.
    uint32_t load_address;
} executable_section_t;

typedef struct
{
    // Up to 8 sections loaded by the BIOS.
    executable_section_t main[8];
    // The count of sections above. Anything above this count is invalid.
    int main_section_count;
    // The address in main RAM where this executable starts running.
    uint32_t main_entrypoint;

    // Up to 8 sections loaded by the BIOS.
    executable_section_t test[8];
    // The count of sections above. Anything above this count is invalid.
    int test_section_count;
    // The address in main RAM where this executable starts running.
    uint32_t test_entrypoint;
} executable_t;

// Read the ROM executable sections out of the header.
void cart_read_executable_info(executable_t *exe);

#ifdef __cplusplus
}
#endif

#endif
