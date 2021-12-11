#ifndef __SYSTEM_H
#define __SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Constants that relate to the entire system (SH-4 specific).
#define UNCACHED_MIRROR 0xA0000000
#define PHYSICAL_MASK 0x0FFFFFFF

// Constants that relate to the Naomi's memory map.
#define BIOS_BASE 0x00000000
#define BIOS_SIZE 0x00200000
#define BIOS_MASK (BIOS_SIZE - 1)

#define SRAM_BASE 0x00200000
#define SRAM_SIZE 0x00008000
#define SRAM_MASK (SRAM_SIZE - 1)

#define SOUNDRAM_BASE 0x00800000
#define SOUNDRAM_SIZE 0x00800000
#define SOUNDRAM_MASK (SOUNDRAM_SIZE - 1)

#define TEXRAM_BASE 0x04000000
#define TEXRAM_SIZE 0x01000000
#define TEXRAM_MASK (TEXRAM_SIZE - 1)

#define VRAM_BASE 0x05000000
#define VRAM_SIZE 0x01000000
#define VRAM_MASK (VRAM_SIZE - 1)

#define RAM_BASE 0x0c000000
#define RAM_SIZE 0x02000000
#define RAM_MASK (RAM_SIZE - 1)

#define STORE_QUEUE_BASE 0xE0000000
#define STORE_QUEUE_SIZE 0x04000000
#define STORE_QUEUE_MASK (STORE_QUEUE_SIZE - 1)

// A 32-byte aligned and 32-byte multiple hardware memset that is about 3x faster than
// the fastest tight loop that you can write in software. Returns nonzero if the copy was
// successful or 0 if the HW was unavailable.
int hw_memset(void *addr, uint32_t value, unsigned int amount);

// A 32-byte aligned and 32-byte multiple hardware memcpy that is similarly faster than
// the fastest tight loop that you can write in software. Returns nonzero if the copy
// was successful or 0 if the HW was unavailable.
int hw_memcpy(void *addr, void *src, unsigned int amount);

// Call code that is outside of our C runtime, such as another program or something
// in the BIOS that does not return. Takes care of safely shutting down interrupts,
// threads and anything else going on so that the new code can execute without any
// of our own calls interfering.
void call_unmanaged(void (*call)());

// Syscalls that request the BIOS do something.
void enter_test_mode();
void restart_game();

// Flush a range of bytes of memory starting at addr from the instruction cache.
void icache_flush_range(void *addr, unsigned int bytes);

#ifdef __cplusplus
}
#endif

#endif
