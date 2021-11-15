#ifndef __SYSTEM_H
#define __SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/stat.h>
#include <dirent.h>

// Constants that relate to the entire system (SH-4 specific).
#define UNCACHED_MIRROR 0xA0000000
#define PHYSICAL_MASK 0x0FFFFFFF

// Constants that relate to the Naomi's memory map.
#define BIOS_BASE 0x00000000
#define BIOS_SIZE 0x00200000

#define SRAM_BASE 0x00200000
#define SRAM_SIZE 0x00008000

#define SOUNDRAM_BASE 0x00800000
#define SOUNDRAM_SIZE 0x00800000

#define TEXRAM_BASE 0x04000000
#define TEXRAM_SIZE 0x01000000

#define VRAM_BASE 0x05000000
#define VRAM_SIZE 0x01000000

#define RAM_BASE 0x0c000000
#define RAM_SIZE 0x02000000

#define STORE_QUEUE_BASE 0xE0000000
#define STORE_QUEUE_SIZE 0x04000000

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

// UTF-8 handling for unicode text.
unsigned int utf8_strlen(const char * const str);
uint32_t *utf8_convert(const char * const str);

// stdin/stdout/stderr redirect hook support so that code using scanf/printf/fprintf
// operates as expected.
typedef struct
{
    int (*stdin_read)( char * const data, unsigned int len );
    int (*stdout_write)( const char * const data, unsigned int len );
    int (*stderr_write)( const char * const data, unsigned int len );
} stdio_t;

// Allow stdin/stdout/stderr to be hooked by external functions. You can hook
// scanf/printf/fprint(stderr) using this function. Only the most recently registered
// stdin hook will be run, but all registered stdout/stderr handlers will be run
// every time there is activity to stdout or stderr.
void * hook_stdio_calls( stdio_t *stdio_calls );
int unhook_stdio_calls( void *prevhook );

// Filesystem redirect hook support so that code using standard file access
// routines can be properly serviced. The below limits are for the number of
// full filesystems that can be attached to the system at one time, as well
// as the total number of open file handles across all of them combined.
#define MAX_FILESYSTEMS 16
#define MAX_OPEN_FILES 256

// Actual struct full of callbacks which will be used to provide the functionality
// of the filesystem. If you do not have an implementation for one or more of these
// functions, set them to NULL and the system code will correctly provide a ENOTSUP
// errno to the calling function. All functions except for open and opendir should
// return the correct return value for the particular hook on success, or a negative
// errno value on failure. This will be passed on to the user code calling the file
// function. For open and opendir, on success a valid pointer to an internal structure
// should be returned. On failure, a negative value representing the negated errno
// should be returned (a cast might be required). For readdir, the dirent entry is
// passed as a pointer to be filled out so that system code can handle allocating
// space for it. On success it should return 1, on encountering the end of directory
// it should return 0 and on failure it should return a negative errno value.
typedef struct
{
    // File handling routines.
    void *(*open)( void *fshandle, const char *name, int flags, int mode );
    int (*fstat)( void *fshandle, void *file, struct stat *st );
    int (*lseek)( void *fshandle, void *file, _off_t amount, int dir );
    int (*read)( void *fshandle, void *file, void *ptr, int len );
    int (*write)( void *fshandle, void *file, const void *ptr, int len );
    int (*close)( void *fshandle, void *file );

    // File name handling routines.
    int (*link)( void *fshandle, const char *oldname, const char *newname );
    int (*mkdir)( void *fshandle, const char *dir, int flags );
    int (*rename)( void *fshandle, const char *oldname, const char *newname );
    int (*unlink)( void *fshandle, const char *name );

    // Directory handling routines.
    void *(*opendir)( void *fshandle, const char *path );
    int (*readdir)( void *fshandle, void *dir, struct dirent *entry );
    int (*seekdir)( void *fshandle, void *dir, int loc );
    int (*closedir)( void *fshandle, void *dir );
} filesystem_t;

// The longest prefix we can accept for a given filesystem.
#define MAX_PREFIX_LEN 27

// Attaches a given filesystem to given prefix. Returns 0 on success or a negative
// value on failure.
int attach_filesystem(const char * const prefix, filesystem_t *filesystem, void *fshandle);

// Detaches the filesystem at a given prefix if it has been attached. Returns 0
// on success or a negative value on failure.
int detach_filesystem(const char * const prefix);

#ifdef __cplusplus
}
#endif

#endif
