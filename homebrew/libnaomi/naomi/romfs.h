#ifndef __ROMFS_H
#define __ROMFS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// The maximum supported ROM filesystems that may be active at any given time.
#define MAX_ROM_FILESYSTEMS 16

// Attach to a ROM FS at a particular cartridge offset, using prefix as the filesystem
// prefix for reading files. If you attach to prefix "romfs", a file at the root of your
// filesystem named "test.txt" would be available at "romfs://test.txt". Returns 0 on
// success (you can use standard open/read/write/close to access the files) or a negative
// number on failure.
int romfs_init(uint32_t rom_offset, char *prefix);

// Detach from a previously attached ROM FS using the prefix given in the init.
void romfs_free(char *prefix);

// Attach to a single ROM FS that is found directly after the executable sections in
// your ROM, and using the prefix "rom". Returns 0 on success (you can use standard
// open/read/write/close to access the files) or a negative number on failure.
int romfs_init_default();

// Detach from the default ROM FS that was attached to in rom_init_default().
void romfs_free_default();

#ifdef __cplusplus
}
#endif

#endif
