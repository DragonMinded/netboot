#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "naomi/system.h"
#include "naomi/romfs.h"
#include "naomi/cart.h"
#include "naomi/interrupt.h"
#include "irqinternal.h"

typedef struct
{
    // The prefix we're registered under.
    char prefix[MAX_PREFIX_LEN + 1];

    // The offset in the ROM where we exist.
    uint32_t rootoffset;

    // The number of entries in the root directory.
    unsigned int rootentries;

    // A pointer to a cached root direntry that was loaded.
    void *rootdir;
} romfs_hook_t;

static romfs_hook_t active_hooks[MAX_ROM_FILESYSTEMS];

#define FILENAME_LEN (256 - 12)

#define ENTRY_TYPE_DIR 1
#define ENTRY_TYPE_FILE 2

typedef struct
{
    // The offset, as relative to the start of the directory that it is in.
    int32_t offset;

    // The size, in bytes if it is a file or in entries if it is a directory.
    uint32_t size;

    // The type of entry.
    uint32_t type;

    // The filename itself.
    char filename[FILENAME_LEN];
} directory_entry_t;

void _romfs_init()
{
    uint32_t old_irq = irq_disable();

    // Just double-check.
    if (sizeof(directory_entry_t) != 256)
    {
        _irq_display_invariant("compile failure", "directory entry structure is %d bytes instead of 256!", sizeof(directory_entry_t));
    }

    memset(active_hooks, 0, sizeof(romfs_hook_t) + MAX_ROM_FILESYSTEMS);
    irq_restore(old_irq);
}

void _romfs_free()
{
    uint32_t old_irq = irq_disable();
    for (int i = 0; i < MAX_ROM_FILESYSTEMS; i++)
    {
        if (active_hooks[i].rootoffset != 0)
        {
            detach_filesystem(active_hooks[i].prefix);
        }
    }
    memset(active_hooks, 0, sizeof(romfs_hook_t) + MAX_ROM_FILESYSTEMS);
    irq_restore(old_irq);
}

directory_entry_t *_romfs_find_entry_in_directory(void *directory, unsigned int entries, const char *filename)
{
    directory_entry_t *dir = directory;
    if (dir == 0)
    {
        return 0;
    }
    
    for (unsigned int i = 0; i < entries; i++)
    {
        if (strcmp(dir[i].filename, filename) == 0)
        {
            return &dir[i];
        }
    }

    return 0;
}

directory_entry_t *_romfs_find_entry(uint32_t rootoffset, void *directory, unsigned int entries, const char *filename, uint32_t *actual_offset)
{
    // Safeguard, don't allow empty filenames.
    if (filename[0] == 0)
    {
        return 0;
    }

    // Grab until we hit a slash or the end of the filename.
    char next_entry[FILENAME_LEN + 1];
    const char *rest = 0;
    unsigned int loc = 0;

    while (loc < FILENAME_LEN)
    {
        if (filename[loc] == '/')
        {
            // Found a directory marker.
            rest = &filename[loc + 1];
            break;
        }
        if (filename[loc] == 0)
        {
            // Found the end of the file, whatever we find is the correct answer.
            rest = 0;
            break;
        }

        next_entry[loc] = filename[loc];
        loc++;
    }

    // Null terminate this, cuz, you know?
    next_entry[loc] = 0;

    // Now, find this entry.
    directory_entry_t *entry = _romfs_find_entry_in_directory(directory, entries, next_entry);
    if (entry == 0)
    {
        // Couldn't find the file at all, give up.
        return 0;
    }

    // Okay, if we don't need to recurse (there was no rest) then this is the answer.
    if (rest == 0 || rest[0] == 0)
    {
        *actual_offset = rootoffset + entry->offset;
        return entry;
    }

    // Okay, we need to recurse and continue looking. So first, we need to make sure that
    // the entry we got back is indeed a directory.
    if (entry->type != ENTRY_TYPE_DIR)
    {
        // Can't recurse, this isn't a directory!
        return 0;
    }

    // Okay, safe to recurse.
    void *dirdata = malloc(entry->size * 256);
    cart_read(dirdata, rootoffset + entry->offset, entry->size * 256); 
    entry = _romfs_find_entry(rootoffset + entry->offset, dirdata, entry->size, rest, actual_offset);
    free(dirdata);
    return entry;
}

directory_entry_t *_romfs_find(romfs_hook_t *hooks, const char *filename, uint32_t *actual_offset)
{
    if (filename[0] != '/')
    {
        // Files MUST be absolute.
        return 0;
    }

    return _romfs_find_entry(hooks->rootoffset, hooks->rootdir, hooks->rootentries, &filename[1], actual_offset);
}

#define CACHED_BLOCK_SIZE 1024

typedef struct
{
    // Nominal information about where to find the data in the ROM.
    uint32_t offset;
    uint32_t size;

    // How far into the data we are currently in our read pointer.
    uint32_t seek;

    // A cached block we've read from the ROM. This is due to the relatively slow
    // data transfer as well as the alignment requirements.
    uint8_t *cachedblock;
    uint32_t cachedoffset;
} open_file_t;

void *_romfs_open(void *fshandle, const char *name, int flags, int mode)
{
    if (flags & O_DIRECTORY)
    {
        // Don't currently support directory listing.
        return (void *)-ENOTSUP;
    }

    // Grab our root filesystem handle, see if we can find the file.
    romfs_hook_t *hooks = (romfs_hook_t *)fshandle;
    uint32_t actual_offset = 0;
    directory_entry_t *entry = _romfs_find(hooks, name, &actual_offset);
    if (entry == 0)
    {
        // File doesn't exist.
        return (void *)-ENOENT;
    }

    // Right now we only support files.
    if (entry->type != ENTRY_TYPE_FILE)
    {
        return (void *)-EISDIR;
    }

    // Okay, create a new open file handle and return that!
    open_file_t *filehandle = malloc(sizeof(open_file_t));
    filehandle->offset = actual_offset;
    filehandle->size = entry->size;
    filehandle->seek = 0;

    // Create cache struture as well.
    filehandle->cachedblock = malloc(CACHED_BLOCK_SIZE);
    filehandle->cachedoffset = 0;
    return filehandle;
}

int _romfs_close(void *fshandle, void *file)
{
    if (fshandle && file)
    {
        open_file_t *filehandle = (open_file_t *)file;

        uint32_t old_irq = irq_disable();
        filehandle->offset = 0;
        filehandle->size = 0;
        filehandle->seek = 0;

        if (filehandle->cachedblock)
        {
            free(filehandle->cachedblock);
            filehandle->cachedblock = 0;
            filehandle->cachedoffset = 0;
        }

        free(file);
        irq_restore(old_irq);

        return 0;
    }
    else
    {
        return -EINVAL;
    }
}

int _romfs_fstat(void *fshandle, void *file, struct stat *st)
{
    if (fshandle && file)
    {
        open_file_t *filehandle = (open_file_t *)file;

        uint32_t old_irq = irq_disable();
        memset(st, 0, sizeof(struct stat));
        st->st_mode = S_IFREG;
        st->st_nlink = 1;
        st->st_size = filehandle->size;
        irq_restore(old_irq);

        return 0;
    }
    else
    {
        return -EINVAL;
    }
}

int _romfs_lseek(void *fshandle, void *file, _off_t amount, int dir)
{
    if (fshandle && file)
    {
        open_file_t *filehandle = (open_file_t *)file;

        uint32_t old_irq = irq_disable();
        int seek_off = 0;

        switch(dir)
        {
            case SEEK_SET:
            {
                if (amount > 0)
                {
                    filehandle->seek = amount;
                }
                else
                {
                    filehandle->seek = 0;
                }

                if (filehandle->seek > filehandle->size)
                {
                    filehandle->seek = filehandle->size;
                }

                seek_off = filehandle->seek;
                break;
            }
            case SEEK_CUR:
            {
                int new_loc = (int)filehandle->seek + amount;

                if (new_loc > 0)
                {
                    filehandle->seek = new_loc;
                }
                else
                {
                    filehandle->seek = 0;
                }

                if (filehandle->seek > filehandle->size)
                {
                    filehandle->seek = filehandle->size;
                }

                seek_off = filehandle->seek;
                break;
            }
            case SEEK_END:
            {
                int new_loc = (int)filehandle->size + amount;

                if (new_loc > 0)
                {
                    filehandle->seek = new_loc;
                }
                else
                {
                    filehandle->seek = 0;
                }

                if (filehandle->seek > filehandle->size)
                {
                    filehandle->seek = filehandle->size;
                }

                seek_off = filehandle->seek;
                break;
            }
            default:
            {
                seek_off = -EINVAL;
                break;
            }
        }

        irq_restore(old_irq);

        return seek_off;
    }
    else
    {
        return -EINVAL;
    }
}

int _romfs_read(void *fshandle, void *file, void *ptr, int len)
{
    if (fshandle && file)
    {
        open_file_t *filehandle = (open_file_t *)file;

        uint32_t maximum = filehandle->offset + filehandle->size;
        uint32_t start = filehandle->offset + filehandle->seek;

        // Make sure we don't overrun our read.
        if (start + len > maximum)
        {
            len = maximum - start;
        }

        uint32_t old_irq = irq_disable();
        int actual_read = 0;

        if (filehandle->cachedblock != 0)
        {
            uint8_t *dstptr = (uint8_t *)ptr;
            while (len > 0)
            {
                // Round to cache location, make sure we have this data.
                uint32_t cache_location = (start / CACHED_BLOCK_SIZE) * CACHED_BLOCK_SIZE;
                if (cache_location != filehandle->cachedoffset)
                {
                    cart_read(filehandle->cachedblock, cache_location, CACHED_BLOCK_SIZE);
                    filehandle->cachedoffset = cache_location;
                }

                // Now, copy as much data as we can out.
                int cache_offset = start - filehandle->cachedoffset;
                int readamount = CACHED_BLOCK_SIZE - cache_offset;
                if (readamount > len)
                {
                    readamount = len;
                }

                memcpy(dstptr, ((uint8_t *)filehandle->cachedblock) + cache_offset, readamount);
                actual_read += readamount;
                start += readamount;
                dstptr += readamount;
                len -= readamount;
            }
        }
        else
        {
            // File was closed in another thread?
            actual_read = -EBADF;
        }

        // Make sure to adjust our seek position.
        if (actual_read >= 0)
        {
            filehandle->seek += actual_read;
        }

        irq_restore(old_irq);
        return actual_read;
    }
    else
    {
        return -EINVAL;
    }
}

static filesystem_t romfs_hooks = {
    _romfs_open,
    _romfs_fstat,
    _romfs_lseek,
    _romfs_read,
    0,  // We don't support write.
    _romfs_close,
    0,  // We don't support link.
    0,  // We don't support mkdir.
    0,  // We don't support rename.
    0,  // We don't support unlink.
};

int romfs_init(uint32_t rom_offset, char *prefix)
{
    // First, read the header, make sure its a ROM FS.
    uint32_t romheaderaligned[4];
    uint8_t *romheader = (uint8_t *)romheaderaligned;

    cart_read(romheader, rom_offset, 16);
    if (memcmp(&romheader[0], "ROMFS", 5) != 0)
    {
        // Not a ROMFS!
        return -1;
    }
    if (romheaderaligned[2] != 0x11291985)
    {
        // Magic value doesn't match!
        return -1;
    }

    // Now, work out the prefix we will be using for this filesystem.
    char actual_prefix[32];

    // Copy the prefix over, make sure it fits within the 27 characters
    // of the filesystem prefix.
    strncpy(actual_prefix, prefix, MAX_PREFIX_LEN - 2);
    actual_prefix[MAX_PREFIX_LEN - 2] = 0;

    // Append ":/" to the end of it.
    strcat(actual_prefix, ":/");

    // Now, find an open slot for our filesystem.
    for (int i = 0; i < MAX_ROM_FILESYSTEMS; i++)
    {
        if (active_hooks[i].rootoffset == 0)
        {
            int retval = attach_filesystem(actual_prefix, &romfs_hooks, &active_hooks[i]);
            if (retval == 0)
            {
                void *rootdir = malloc(256 * romheaderaligned[3]);
                cart_read(rootdir, rom_offset + 16, 256 * romheaderaligned[3]);

                // It worked! Mark this filesystem as active!
                active_hooks[i].rootoffset = rom_offset + 16;
                active_hooks[i].rootentries = romheaderaligned[3];
                active_hooks[i].rootdir = rootdir;
                strcpy(active_hooks[i].prefix, actual_prefix);
            }

            return retval;
        }
    }

    // No room for a new ROMFS.
    return -1;
}

int romfs_init_default()
{
    // Find the highest section, grab the data directly after that
    // aligned by 4 and assume that's the ROM FS.
    executable_t exe;
    cart_read_executable_info(&exe);

    // Find the highest section that is loaded by the BIOS, if we
    // were constructed correctly then the next data chunk after that
    // aligned to 4 bytes will be our ROM FS.
    uint32_t offset = 0;
    for (int i = 0; i < exe.main_section_count; i++)
    {
        uint32_t end = exe.main[i].offset + exe.main[i].length;
        offset = offset > end ? offset : end;
    }
    for (int i = 0; i < exe.main_section_count; i++)
    {
        uint32_t end = exe.main[i].offset + exe.main[i].length;
        offset = offset > end ? offset : end;
    }

    // Round to nearest 4 bytes.
    offset = (offset + 3) & 0xFFFFFFFC;

    // Now initialize it.
    return romfs_init(offset, "rom");
}

void romfs_free(char *prefix)
{
    char actual_prefix[32];

    // Copy the prefix over, make sure it fits within the 27 characters
    // of the filesystem prefix.
    strncpy(actual_prefix, prefix, MAX_PREFIX_LEN - 2);
    actual_prefix[MAX_PREFIX_LEN - 2] = 0;

    // Append ":/" to the end of it.
    strcat(actual_prefix, ":/");

    // Unregister ourselves as this FS!
    for (int i = 0; i < MAX_ROM_FILESYSTEMS; i++)
    {
        if (active_hooks[i].rootoffset != 0 && strcmp(actual_prefix, active_hooks[i].prefix) == 0)
        {
            detach_filesystem(active_hooks[i].prefix);
            if (active_hooks[i].rootdir)
            {
                free(active_hooks[i].rootdir);
            }
            memset(&active_hooks[i], 0, sizeof(romfs_hook_t));
        }
    }
}

void romfs_free_default()
{
    romfs_free("rom");
}
