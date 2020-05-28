#ifndef __SYSTEM_H
#define __SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

// Constants that relate to the entire system (SH-4 specific).
#define UNCACHED_MIRROR 0xA0000000
#define PHYSICAL_MASK 0x0FFFFFFF

// Constants that relate to the Naomi's memory map.
#define BIOS_BASE 0x00000000
#define BIOS_SIZE 0x200000

#define SRAM_BASE 0x00200000
#define SRAM_SIZE 0x8000

#define SOUNDRAM_BASE 0x00800000
#define SOUNDRAM_SIZE 0x800000

#define VRAM_BASE 0x05000000
#define VRAM_SIZE 0x1000000

#define RAM_BASE 0x0c000000
#define RAM_SIZE 0x2000000

#ifdef __cplusplus
}
#endif

#endif
