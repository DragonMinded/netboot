#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/reent.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <string.h>
#include "naomi/system.h"
#include "naomi/maple.h"
#include "naomi/timer.h"
#include "naomi/rtc.h"
#include "naomi/interrupt.h"
#include "naomi/thread.h"
#include "naomi/audio.h"
#include "naomi/video.h"
#include "irqinternal.h"

#define CCR (*(uint32_t *)0xFF00001C)
#define QACR0 (*(uint32_t *)0xFF000038)
#define QACR1 (*(uint32_t *)0xFF00003C)

#define SYSCALL_VECTOR_BASE ((uint32_t *)0xAC018000)
#define SYSCALL_CALCULATE_EEPROM_SETTINGS 0x4
#define SYSCALL_READ_AND_PERFORM_DIMM_COMMAND 0x9
#define SYSCALL_ENTER_TEST_MODE 0x11
#define SYSCALL_POLL_HAS_DIMM_COMMAND 0x14

/* Actual definition of global errno */
int errno;

extern int main();
extern int test();

/* C++ expected definitions */
void *__dso_handle = NULL;

/* Global constructors/destructors */
typedef void (*func_ptr)(void);
extern uint32_t __ctors;
extern uint32_t __ctors_end;
extern uint32_t __dtors;
extern uint32_t __dtors_end;

/* libgcc floating point stuff */
extern void __set_fpscr (unsigned long);

/* Global hardware access mutexes. */
static mutex_t queue_mutex;

/* Provide a weakref to a default test sub for autoconf-purposes. */
int __test()
{
    return 0;
}

int test() __attribute__((weak, alias ("__test")));

void _exit(int status)
{
    // Run fini sections.
    uint32_t *dtor_ptr = &__dtors;
    while (dtor_ptr < &__dtors_end)
    {
        (*((func_ptr *)dtor_ptr))();
        dtor_ptr++;
    }

    // We don't have an OS to "go back to", so just infinite-loop.
    while ( 1 ) { ; }
}

// Prototypes of functions that we don't want available in the public headers
void _irq_init();
void _irq_free();
void _maple_init();
void _maple_free();
void _timer_init();
void _timer_free();
void _thread_init();
void _thread_free();
void _fs_init();
void _fs_free();
void _romfs_init();
void _romfs_free();

void _enter()
{
    // We set this to 1 or 0 depending on whether we are in test or normal
    // mode. Save this value locally since the register could change in
    // global constructors below.
    register uint32_t boot_mode asm("r3");
    uint32_t _boot_mode = boot_mode;

    // Invalidate cache, as is done in real games.
    CCR = 0x905;

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

    // Set up floating point stuff. Requests round to nearest instead of round to zero,
    // denormalized numbers treated as zero.
    __set_fpscr(0x40000);

    // Run init sections.
    uint32_t *ctor_ptr = &__ctors;
    while (ctor_ptr < &__ctors_end)
    {
        (*((func_ptr *)ctor_ptr))();
        ctor_ptr++;
    }

    // Initialize things we promise are fully ready by the time main/test is called.
    _timer_init();
    _thread_init();
    _maple_init();
    _irq_init();
    _fs_init();
    _romfs_init();

    // Initialize mutexes for hardware that needs exclusive access.
    mutex_init(&queue_mutex);

    // Execute main/test executable based on boot variable set in
    // sh-crt0.s which comes from the entrypoint used to start the code.
    int status;
    if(_boot_mode == 0)
    {
        status = main();
    }
    else
    {
        status = test();
    }

    // Free anything that was possibly initialized by the user.
    audio_free();
    video_free();

    // Free those things now that we're done. We should usually never get here
    // because it would be unusual to exit from main/test by returning.
    _romfs_free();
    _fs_free();
    _irq_free();
    _maple_free();
    _thread_free();
    _timer_free();

    // Finally, exit from the program.
    _exit(status);
}

int hw_memset(void *addr, uint32_t value, unsigned int amount)
{
    // Very similar to a standard memset, but the address pointer must be aligned
    // to a 32 byte boundary, the amount must be a multiple of 32 bytes and the
    // value must be 32 bits. When used correctly this is about 3x faster than
    // a software memset.

    if ((((uint32_t)addr) & 0x1F) != 0)
    {
        _irq_display_invariant("invalid hw_memset location", "addr %08lx is not aligned to 32-byte boundary", (uint32_t)addr);
    }
    if ((amount & 0x1F) != 0)
    {
        _irq_display_invariant("invalid hw_memset amount", "amount %lu is not multiple of 32 bytes", amount);
    }

    int successful = 0;
    if (mutex_try_lock(&queue_mutex))
    {
        // Set the base queue address pointer to the queue location with address bits
        // 25:5. The bottom bits should be all 0s since hw_memset requires an alignment
        // to a 32 byte boundary. We will use both queue areas since SQ0/SQ1 specification
        // is the same bit as address bit 5. Technically this means the below queue setup
        // interleaves the data between the two queues, but it really does not matter what
        // order the hardware copies things.
        uint32_t *queue = (uint32_t *)(STORE_QUEUE_BASE | (((uint32_t)addr) & 0x03FFFFE0));
        uint32_t actual_copy_addr = (uint32_t)addr;
        uint32_t stored_addr_bits = (actual_copy_addr >> 24) & 0x1C;

        // Set the top address bits (28:26) into the store queue address control registers.
        QACR0 = stored_addr_bits;
        QACR1 = stored_addr_bits;

        // Now, set up both store queues to contain the same value that we want to memset.
        // This is 8 32-bit values per store queue.
        for (int i = 0; i < 16; i++) {
            queue[i] = value;
        }

        // Now, trigger the hardware to copy the values from the queue to the address we
        // care about, triggering one 32-byte prefetch at a time.
        for (int cycles = amount >> 5; cycles > 0; cycles--)
        {
            // Make sure we don't wrap around our top address bits.
            if (((actual_copy_addr >> 24) & 0x1C) != stored_addr_bits)
            {
                // Re-init the top address control registers and the queue.
                stored_addr_bits = (actual_copy_addr >> 24) & 0x1C;
                QACR0 = stored_addr_bits;
                QACR1 = stored_addr_bits;

                // Now, set up both store queues to contain the same value that we want to memset.
                // This is 8 32-bit values per store queue.
                for (int i = 0; i < 16; i++) {
                    queue[i] = value;
                }
            }

            // Perform the actual memset.
            __asm__("pref @%0" : : "r"(queue));
            queue += 8;
            actual_copy_addr += 32;
        }

        // Finally, attempt a new write to both queues in order to stall the CPU until the
        // last write is done.
        queue = (uint32_t *)STORE_QUEUE_BASE;
        queue[0] = 0;
        queue[8] = 0;

        // We held the lock and succeeded at memsetting.
        successful = 1;
        mutex_unlock(&queue_mutex);
    }

    return successful;
}

int hw_memcpy(void *dest, void *src, unsigned int amount)
{
    // Very similar to a standard memcpy, but the destination pointer must be aligned
    // to a 32 byte boundary, the amount must be a multiple of 32 bytes and the source
    // pointer must be aligned to a 4 byte boundary.

    if ((((uint32_t)dest) & 0x1F) != 0)
    {
        _irq_display_invariant("invalid hw_memcpy location", "dest %08lx is not aligned to 32-byte boundary", (uint32_t)dest);
    }
    if ((((uint32_t)src) & 0x3) != 0)
    {
        _irq_display_invariant("invalid hw_memcpy location", "src %08lx is not aligned to 32-byte boundary", (uint32_t)src);
    }
    if ((amount & 0x1F) != 0)
    {
        _irq_display_invariant("invalid hw_memcpy amount", "amount %lu is not multiple of 32 bytes", amount);
    }

    int successful = 0;
    if (mutex_try_lock(&queue_mutex))
    {

        // Set the base queue address pointer to the queue location with destination bits
        // 25:5. The bottom bits should be all 0s since hw_memset requires an alignment
        // to a 32 byte boundary. We will use both queue areas since SQ0/SQ1 specification
        // is the same bit as destination bit 5. Technically this means the below queue setup
        // interleaves the data between the two queues, but it really does not matter what
        // order the hardware copies things.
        uint32_t *srcptr = (uint32_t *)src;
        uint32_t *queue = (uint32_t *)(STORE_QUEUE_BASE | (((uint32_t)dest) & 0x03FFFFE0));
        uint32_t actual_copy_dest = (uint32_t)dest;
        uint32_t stored_dest_bits = (actual_copy_dest >> 24) & 0x1C;

        // Set the top address bits (28:26) into the store queue address control registers.
        QACR0 = stored_dest_bits;
        QACR1 = stored_dest_bits;

        // Now, trigger the hardware to copy the values from the queue to the address we
        // care about, triggering one 32-byte prefetch at a time.
        for (int cycles = amount >> 5; cycles > 0; cycles--)
        {
            // Make sure we don't wrap around if we were near a memory border.
            if (((actual_copy_dest >> 24) & 0x1C) != stored_dest_bits)
            {
                // Re-init the top address control registers and the queue.
                stored_dest_bits = (actual_copy_dest >> 24) & 0x1C;
                QACR0 = stored_dest_bits;
                QACR1 = stored_dest_bits;
            }

            // First, prefetch the bytes we will need in the next cycle.
            __asm__("pref @%0" : : "r"(srcptr + 8));

            // Now, load the destination queue with the next 32 bytes from the source.
            queue[0] = *srcptr++;
            queue[1] = *srcptr++;
            queue[2] = *srcptr++;
            queue[3] = *srcptr++;
            queue[4] = *srcptr++;
            queue[5] = *srcptr++;
            queue[6] = *srcptr++;
            queue[7] = *srcptr++;

            // Finally, trigger the store of this data
            __asm__("pref @%0" : : "r"(queue));
            queue += 8;
            actual_copy_dest += 32;
        }

        // Finally, attempt a new write to both queues in order to stall the CPU until the
        // last write is done.
        queue = (uint32_t *)STORE_QUEUE_BASE;
        queue[0] = 0;
        queue[8] = 0;

        // We held the lock and succeeded at memsetting.
        successful = 1;
        mutex_unlock(&queue_mutex);
    }

    return successful;
}

void call_unmanaged(void (*call)())
{
    // Free anything that was possibly initialized by the user.
    audio_free();
    video_free();

    // Shut down everything since we're leaving our executable.
    _romfs_free();
    _fs_free();
    _irq_free();
    _maple_free();
    _thread_free();
    _timer_free();

    // Call it.
    call();

    // Finally, exit from the program if it ever returns.
    _exit(0);
}

void enter_test_mode()
{
    // Look up the address of the enter test mode syscall.
    uint32_t test_mode_syscall = SYSCALL_VECTOR_BASE[SYSCALL_ENTER_TEST_MODE] | UNCACHED_MIRROR;

    // Call it.
    call_unmanaged((void (*)())test_mode_syscall);
}

// Currently hooked stdio calls.
static stdio_t stdio_hooks = { 0, 0, 0 };

int hook_stdio_calls( stdio_t *stdio_calls )
{
    if( stdio_calls == NULL )
    {
        /* Failed to hook, bad input */
        return -1;
    }

    /* Safe to hook */
    if (stdio_calls->stdin_read)
    {
        stdio_hooks.stdin_read = stdio_calls->stdin_read;
    }
    if (stdio_calls->stdout_write)
    {
        stdio_hooks.stdout_write = stdio_calls->stdout_write;
    }
    if (stdio_calls->stderr_write)
    {
        stdio_hooks.stderr_write = stdio_calls->stderr_write;
    }

    /* Success */
    return 0;
}

int unhook_stdio_calls( stdio_t *stdio_calls )
{
    /* Just wipe out internal variable */
    if (stdio_calls->stdin_read == stdio_hooks.stdin_read)
    {
        stdio_hooks.stdin_read = 0;
    }
    if (stdio_calls->stdout_write == stdio_hooks.stdout_write)
    {
        stdio_hooks.stdout_write = 0;
    }
    if (stdio_calls->stderr_write == stdio_hooks.stderr_write)
    {
        stdio_hooks.stderr_write = 0;
    }

    /* Always successful for now */
    return 0;
}

#define FS_PREFIX_LEN 28

typedef struct
{
    /* Pointer to the filesystem callbacks for this filesystem. */
    filesystem_t *fs;
    /* Opaque pointer of data that is passed to us from attach_filesystem and
     * we pass back to the filesystem hooks on every call. */
    void *fshandle;
    /* Filesystem prefix, such as 'rom:/' or 'mem:/' that this filesystem is
     * found under when using standard library file routines. */
    char prefix[FS_PREFIX_LEN];
} fs_mapping_t;

typedef struct
{
    /* Index into the filesystem master mapping to get a fs_mapping_t. */
    int fs_mapping;
    /* The handle returned from the filesystem code's open() function which will
     * be passed to all other function calls. */
    void *handle;
    /* The handle as returned to newlib which will be given to all userspace
     * code calling standard file routines. */
    int fileno;
} fs_handle_t;

static fs_mapping_t filesystems[MAX_FILESYSTEMS];
static fs_handle_t handles[MAX_OPEN_HANDLES];

void _fs_init()
{
    uint32_t old_irq = irq_disable();
    memset(filesystems, 0, sizeof(fs_mapping_t) * MAX_FILESYSTEMS);
    memset(handles, 0, sizeof(fs_handle_t) * MAX_OPEN_HANDLES);
    irq_restore(old_irq);
}

void _fs_free()
{
    uint32_t old_irq = irq_disable();

    // Go through and close all open file handles for all filesystems.
    for (int j = 0; j < MAX_OPEN_HANDLES; j++)
    {
        if (handles[j].fileno > 0 && handles[j].handle != 0)
        {
            filesystems[handles[j].fs_mapping].fs->close(filesystems[handles[j].fs_mapping].fshandle, handles[j].handle);
        }
    }

    memset(filesystems, 0, sizeof(fs_mapping_t) * MAX_FILESYSTEMS);
    memset(handles, 0, sizeof(fs_handle_t) * MAX_OPEN_HANDLES);
    irq_restore(old_irq);
}

int attach_filesystem(const char * const prefix, filesystem_t *filesystem, void *fshandle)
{
    /* Sanity checking */
    if (!prefix || !filesystem)
    {
        return -1;
    }

    /* Make sure prefix is valid */
    int len = strlen(prefix);
    if (len < 3 || len >= FS_PREFIX_LEN || prefix[len - 1] != '/' || prefix[len - 2] != ':')
    {
        return -1;
    }

    /* Make sure the prefix doesn't match one thats already inserted */
    for (int i = 0; i < MAX_FILESYSTEMS; i++)
    {
        if (filesystems[i].prefix[0] != 0 && strcmp(filesystems[i].prefix, prefix) == 0)
        {
            /* Filesystem has already been inserted */
            return -2;
        }
    }

    /* Find an open filesystem entry */
    for (int i = 0; i < MAX_FILESYSTEMS; i++ )
    {
        if (filesystems[i].prefix[0] == 0)
        {
            /* Attach the prefix, remember the pointers to the fs functions. */
            strcpy(filesystems[i].prefix, prefix);
            filesystems[i].fs = filesystem;
            filesystems[i].fshandle = fshandle;
            return 0;
        }
    }

    /* No more filesystem handles available */
    return -3;
}

int detach_filesystem( const char * const prefix )
{
    /* Sanity checking */
    if (prefix == 0)
    {
        return -1;
    }

    for (int i = 0; i < MAX_FILESYSTEMS; i++)
    {
        if (filesystems[i].prefix[0] != 0 && strcmp(filesystems[i].prefix, prefix) == 0)
        {
            if (filesystems[i].fs->close != 0)
            {
                /* We found the filesystem, now go through and close every open file handle */
                for (int j = 0; j < MAX_OPEN_HANDLES; j++)
                {
                    if (handles[j].fileno > 0 && handles[j].fs_mapping == i && handles[j].handle != 0)
                    {
                        filesystems[i].fs->close(filesystems[i].fshandle, handles[j].handle);
                    }
                }
            }

            /* Now zero out the filesystem entry so it can't be found. */
            memset(&filesystems[i], 0, sizeof(fs_mapping_t));

            /* All went well */
            return 0;
        }
    }

    /* Couldn't find the filesystem to free */
    return -2;
}

int _fs_next_handle()
{
    /* Start past STDIN, STDOUT, STDERR file handles */
    static int handle = 3;
    int newhandle;

    /* Make sure we don't screw up and give the same file handle to multiple threads. */
    uint32_t old_irq = irq_disable();
    newhandle = handle++;
    irq_restore(old_irq);

    return newhandle;
}

int _fs_get_hooks(int fileno, filesystem_t **fs, void **fshandle, void **handle)
{
    if( fileno < 3 )
    {
        return 0;
    }

    for (int i = 0; i < MAX_OPEN_HANDLES; i++)
    {
        if (handles[i].fileno == fileno)
        {
            // Found it!
            *fs = filesystems[handles[i].fs_mapping].fs;
            *fshandle = filesystems[handles[i].fs_mapping].fshandle;
            *handle = handles[i].handle;
            return 1;
        }
    }

    // Couldn't find it.
    return 0;
}

int _fs_get_fs_by_name(const char * const name)
{
    /* Invalid */
    if (name == 0)
    {
        return -1;
    }

    for(int i = 0; i < MAX_FILESYSTEMS; i++)
    {
        if (filesystems[i].prefix[0] != 0 && strncmp(filesystems[i].prefix, name, strlen(filesystems[i].prefix)) == 0)
        {
            /* Found it! */
            return i;
        }
    }

    /* Couldn't find it */
    return -1;

}

_ssize_t _read_r(struct _reent *reent, int file, void *ptr, size_t len)
{
    if( file == 0 )
    {
        if( stdio_hooks.stdin_read )
        {
            return stdio_hooks.stdin_read( ptr, len );
        }
        else
        {
            /* No hook for this */
            reent->_errno = EBADF;
            return -1;
        }
    }
    else if( file == 1 || file == 2 )
    {
        /* Can't read from output buffers */
        reent->_errno = EBADF;
        return -1;
    }
    else
    {
        /* Attempt to use filesystem hooks to perform read */
        filesystem_t *fs = 0;
        void *fshandle = 0;
        void *handle = 0;
        if (_fs_get_hooks(file, &fs, &fshandle, &handle))
        {
            if (fs->read == 0)
            {
                /* Filesystem doesn't support read */
                reent->_errno = ENOTSUP;
                return -1;
            }

            int retval = fs->read(fshandle, handle, ptr, len);
            if (retval < 0)
            {
                reent->_errno = -retval;
                return -1;
            }
            else
            {
                return retval;
            }
        }

        /* There is no filesystem backing this file. */
        reent->_errno = ENOTSUP;
        return -1;
    }
}

_off_t _lseek_r(struct _reent *reent, int file, _off_t amount, int dir)
{
    /* Attempt to use filesystem hooks to perform lseek */
    filesystem_t *fs = 0;
    void *fshandle = 0;
    void *handle = 0;
    if (_fs_get_hooks(file, &fs, &fshandle, &handle))
    {
        if (fs->lseek == 0)
        {
            /* Filesystem doesn't support lseek */
            reent->_errno = ENOTSUP;
            return -1;
        }

        int retval = fs->lseek(fshandle, handle, amount, dir);
        if (retval < 0)
        {
            reent->_errno = -retval;
            return -1;
        }
        else
        {
            return retval;
        }
    }

    /* There is no filesystem backing this file. */
    reent->_errno = ENOTSUP;
    return -1;
}

_ssize_t _write_r(struct _reent *reent, int file, const void * ptr, size_t len)
{
    if( file == 0 )
    {
        /* Can't write to input buffers */
        reent->_errno = EBADF;
        return -1;
    }
    else if( file == 1 )
    {
        if( stdio_hooks.stdout_write )
        {
            return stdio_hooks.stdout_write( ptr, len );
        }
        else
        {
            reent->_errno = EBADF;
            return -1;
        }
    }
    else if( file == 2 )
    {
        if( stdio_hooks.stderr_write )
        {
            return stdio_hooks.stderr_write( ptr, len );
        }
        else
        {
            reent->_errno = EBADF;
            return -1;
        }
    }
    else
    {
        /* Attempt to use filesystem hooks to perform write */
        filesystem_t *fs = 0;
        void *fshandle = 0;
        void *handle = 0;
        if (_fs_get_hooks(file, &fs, &fshandle, &handle))
        {
            if (fs->write == 0)
            {
                /* Filesystem doesn't support write */
                reent->_errno = ENOTSUP;
                return -1;
            }

            int retval = fs->write(fshandle, handle, ptr, len);
            if (retval < 0)
            {
                reent->_errno = -retval;
                return -1;
            }
            else
            {
                return retval;
            }
        }

        /* There is no filesystem backing this file. */
        reent->_errno = ENOTSUP;
        return -1;
    }
}

int _close_r(struct _reent *reent, int file)
{
    /* Attempt to use filesystem hooks to perform close */
    filesystem_t *fs = 0;
    void *fshandle = 0;
    void *handle = 0;
    if (_fs_get_hooks(file, &fs, &fshandle, &handle))
    {
        int retval;
        if (fs->close == 0)
        {
            /* Filesystem doesn't support close */
            reent->_errno = ENOTSUP;
            retval = -1;
        }
        else
        {
            /* Perform the close action. */
            retval = fs->close(fshandle, handle);
        }

        /* Finally, before we return, unregister this handle. */
        for( int i = 0; i < MAX_OPEN_HANDLES; i++)
        {
            if (handles[i].fileno == file)
            {
                memset(&handles[i], 0, sizeof(fs_handle_t));
            }
        }

        if (retval < 0)
        {
            reent->_errno = -retval;
            return -1;
        }
        else
        {
            return retval;
        }
    }

    /* There is no filesystem backing this file. */
    reent->_errno = ENOTSUP;
    return -1;
}

int _link_r(struct _reent *reent, const char *old, const char *new)
{
    /* Attempt to use filesystem hooks to perform link */
    int oldfs = _fs_get_fs_by_name(old);
    int newfs = _fs_get_fs_by_name(old);

    if (oldfs >= 0 && newfs >= 0)
    {
        /* Make sure both of them are of the same filesystem. */
        if (oldfs != newfs)
        {
            /* We can't link across multiple filesytems. What are we, linux? */
            reent->_errno = ENOTSUP;
            return -1;
        }

        filesystem_t *fs = filesystems[oldfs].fs;
        if (fs->link == 0)
        {
            /* Filesystem doesn't support link */
            reent->_errno = ENOTSUP;
            return -1;
        }

        int retval = fs->link(filesystems[oldfs].fshandle, old + strlen(filesystems[oldfs].prefix), new + strlen(filesystems[newfs].prefix));
        if (retval < 0)
        {
            reent->_errno = -retval;
            return -1;
        }
        else
        {
            return retval;
        }
    }

    /* There is no filesystem backing this file. */
    reent->_errno = ENOTSUP;
    return -1;
}

int _rename_r (struct _reent *reent, const char *old, const char *new)
{
    /* Attempt to use filesystem hooks to perform rename */
    int oldfs = _fs_get_fs_by_name(old);
    int newfs = _fs_get_fs_by_name(old);

    if (oldfs >= 0 && newfs >= 0)
    {
        /* Make sure both of them are of the same filesystem. */
        if (oldfs != newfs)
        {
            /* We can't rename across multiple filesytems. What are we, linux? */
            reent->_errno = ENOTSUP;
            return -1;
        }

        filesystem_t *fs = filesystems[oldfs].fs;
        if (fs->rename == 0)
        {
            /* Filesystem doesn't support rename */
            reent->_errno = ENOTSUP;
            return -1;
        }

        int retval = fs->rename(filesystems[oldfs].fshandle, old + strlen(filesystems[oldfs].prefix), new + strlen(filesystems[newfs].prefix));
        if (retval < 0)
        {
            reent->_errno = -retval;
            return -1;
        }
        else
        {
            return retval;
        }
    }

    /* There is no filesystem backing this file. */
    reent->_errno = ENOTSUP;
    return -1;
}

void *_sbrk_impl(struct _reent *reent, ptrdiff_t incr)
{
    extern char end;  /* Defined by the linker in naomi.ld */
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

void *_sbrk_r(struct _reent *reent, ptrdiff_t incr)
{
    uint32_t old_interrupts = irq_disable();
    void *ptr = _sbrk_impl(reent, incr);
    irq_restore(old_interrupts);
    return ptr;
}

int _fstat_r(struct _reent *reent, int file, struct stat *st)
{
    /* Attempt to use filesystem hooks to perform fstat */
    filesystem_t *fs = 0;
    void *fshandle = 0;
    void *handle = 0;
    if (_fs_get_hooks(file, &fs, &fshandle, &handle))
    {
        if (fs->fstat == 0)
        {
            /* Filesystem doesn't support fstat */
            reent->_errno = ENOTSUP;
            return -1;
        }

        int retval = fs->fstat(fshandle, handle, st);
        if (retval < 0)
        {
            reent->_errno = -retval;
            return -1;
        }
        else
        {
            return retval;
        }
    }

    /* There is no filesystem backing this file. */
    reent->_errno = ENOTSUP;
    return -1;
}

int _mkdir_r(struct _reent *reent, const char *path, int flags)
{
    /* Attempt to use filesystem hooks to perform mkdir */
    int mapping = _fs_get_fs_by_name(path);
    if (mapping >= 0)
    {
        filesystem_t *fs = filesystems[mapping].fs;
        if (fs->mkdir == 0)
        {
            /* Filesystem doesn't support mkdir */
            reent->_errno = ENOTSUP;
            return -1;
        }

        int retval = fs->mkdir(filesystems[mapping].fshandle, path + strlen(filesystems[mapping].prefix), flags);
        if (retval < 0)
        {
            reent->_errno = -retval;
            return -1;
        }
        else
        {
            return retval;
        }
    }

    /* There is no filesystem backing this file. */
    reent->_errno = ENOTSUP;
    return -1;
}

int _open_r(struct _reent *reent, const char *path, int flags, int mode)
{
    int mapping = _fs_get_fs_by_name(path);
    filesystem_t *fs = 0;

    if (mapping >= 0)
    {
        fs = filesystems[mapping].fs;
    }
    else
    {
        /* There is no fileystem backing this path. */
        reent->_errno = ENOTSUP;
        return -1;
    }

    if (fs->open == 0)
    {
        /* Filesystem doesn't support open */
        reent->_errno = ENOTSUP;
        return -1;
    }

    /* Do we have room for a new file? */
    for (int i = 0; i < MAX_OPEN_HANDLES; i++)
    {
        if (handles[i].fileno == 0)
        {
            /* Yes, we have room, try the open */
            void *ptr = fs->open(filesystems[mapping].fshandle, path + strlen(filesystems[mapping].prefix), flags, mode);
            int errnoptr = (int)ptr;

            if (errnoptr > 0)
            {
                /* Create new internal handle */
                handles[i].fileno = _fs_next_handle();
                handles[i].handle = ptr;
                handles[i].fs_mapping = mapping;

                /* Return our own handle */
                return handles[i].fileno;
            }
            else
            {
                /* Couldn't open for some reason */
                if (errnoptr == 0)
                {
                    reent->_errno = ENOENT;
                }
                else
                {
                    reent->_errno = -errnoptr;
                }
                return -1;
            }
        }
    }

    /* No file handles available */
    reent->_errno = ENFILE;
    return -1;
}

int _unlink_r(struct _reent *reent, const char *path)
{
    /* Attempt to use filesystem hooks to perform unlink */
    int mapping = _fs_get_fs_by_name(path);
    if (mapping >= 0)
    {
        filesystem_t *fs = filesystems[mapping].fs;
        if (fs->unlink == 0)
        {
            /* Filesystem doesn't support unlink */
            reent->_errno = ENOTSUP;
            return -1;
        }

        int retval = fs->unlink(filesystems[mapping].fshandle, path + strlen(filesystems[mapping].prefix));
        if (retval < 0)
        {
            reent->_errno = -retval;
            return -1;
        }
        else
        {
            return retval;
        }
    }

    /* There is no filesystem backing this file. */
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
    // TODO: Implement kill once we have threads support.
    reent->_errno = ENOTSUP;
    return -1;
}

int _getpid_r(struct _reent *reent)
{
    // TODO: Implement getpid once we have threads support.
    reent->_errno = ENOTSUP;
    return -1;
}

int _stat_r(struct _reent *reent, const char *path, struct stat *st)
{
    /* Attempt to use filesystem hooks to perform stat */
    int mapping = _fs_get_fs_by_name(path);
    if (mapping >= 0)
    {
        filesystem_t *fs = filesystems[mapping].fs;
        if (fs->open == 0 || fs->close == 0 || fs->fstat == 0)
        {
            /* Filesystem doesn't support stat by way of missing utility functions */
            reent->_errno = ENOTSUP;
            return -1;
        }

        /* Open the file, grab the stat, close it */
        void *handle = fs->open(filesystems[mapping].fshandle, path + strlen(filesystems[mapping].prefix), 0, 0666);
        int handleint = (int)handle;

        if (handleint > 0)
        {
            int retval = fs->fstat(filesystems[mapping].fshandle, handle, st);
            fs->close(filesystems[mapping].fshandle, handle);

            /* Return what stat gave us */
            if (retval < 0)
            {
                reent->_errno = -retval;
                return -1;
            }
            else
            {
                return retval;
            }
        }
        else
        {
            reent->_errno = -handleint;
            return -1;
        }
    }

    /* There is no filesystem backing this file. */
    reent->_errno = ENOTSUP;
    return -1;
}

int _fork_r(struct _reent *reent)
{
    // TODO: Implement fork once we have threads support.
    reent->_errno = ENOTSUP;
    return -1;
}

int _wait_r(struct _reent *reent, int *statusp)
{
    // TODO: Implement wait once we have threads support.
    reent->_errno = ENOTSUP;
    return -1;
}

int _execve_r(struct _reent *reent, const char *path, char *const argv[], char *const envp[])
{
    // TODO: Implement execve once we have threads support.
    reent->_errno = ENOTSUP;
    return -1;
}

_CLOCK_T_ _times_r(struct _reent *reent, struct tms *tm)
{
    // TODO: Implement times once we have threads support.
    reent->_errno = ENOTSUP;
    return -1;
}

// Amount of seconds in twenty years not spanning over a century rollover.
// We use this because RTC epoch on Naomi is 1/1/1950 instead of 1/1/1970
// like unix and C standard library expects.
#define TWENTY_YEARS ((20 * 365LU + 5) * 86400)

int _gettimeofday_r(struct _reent *reent, struct timeval *tv, void *tz)
{
    tv->tv_sec = rtc_get() - TWENTY_YEARS;
    tv->tv_usec = 0;
    return 0;
}

typedef struct
{
    void *owner;
    int depth;
    uint32_t old_irq;
} recursive_newlib_lock_t;

recursive_newlib_lock_t newlib_lock = { 0, 0 };

void __malloc_lock (struct _reent *reent)
{
    uint32_t old_irq = irq_disable();

    if (newlib_lock.owner == reent)
    {
        // Increase our depth.
        newlib_lock.depth++;

        // No need to unlock interrupts here, we've already disabled them in the
        // first lock.
        return;
    }
    if (newlib_lock.owner != 0)
    {
        _irq_display_invariant("malloc locking failure", "malloc lock owned by another malloc call during lock!");
    }

    // Lock ourselves, remembering our old IRQ.
    newlib_lock.owner = reent;
    newlib_lock.depth = 1;
    newlib_lock.old_irq = old_irq;
}

void __malloc_unlock (struct _reent *reent)
{
    // Just in case, but we shouldn't have to worry about IRQs being enabled
    // if newlib is coded correctly.
    irq_disable();

    if (newlib_lock.owner != reent)
    {
        _irq_display_invariant("malloc locking failure", "malloc lock owned by another malloc call during unlock!");
    }

    newlib_lock.depth --;
    if (newlib_lock.depth == 0)
    {
        // Time to unlock here!
        newlib_lock.owner = 0;
        irq_restore(newlib_lock.old_irq);
    }
}

unsigned int utf8_strlen(const char * const str)
{
    uint8_t *data = (uint8_t *)str;
    unsigned int max = strlen(str);
    unsigned int len = 0;
    unsigned int pos = 0;

    while (pos < max)
    {
        if ((data[pos] & 0x80) == 0)
        {
            len ++;
            pos ++;
        }
        else if((data[pos] & 0xE0) == 0xC0)
        {
            len ++;
            pos += 2;
        }
        else if((data[pos] & 0xF0) == 0xE0)
        {
            len ++;
            pos += 3;
        }
        else if((data[pos] & 0xF1) == 0xF0)
        {
            len ++;
            pos += 4;
        }
        else
        {
            // Error!
            return 0;
        }
    }

    return len;
}

uint32_t *utf8_convert(const char * const str)
{
    // First make some room for the output.
    unsigned int length = utf8_strlen(str);
    uint32_t *chars = malloc(sizeof(uint32_t) * (length + 1));
    if (chars == 0)
    {
        return 0;
    }
    memset(chars, 0, sizeof(uint32_t) * (length + 1));

    // Now, convert characters one at a time.
    uint8_t *data = (uint8_t *)str;
    unsigned int inpos = 0;
    unsigned int outpos = 0;

    while (outpos < length)
    {
        if ((data[inpos] & 0x80) == 0)
        {
            chars[outpos] = data[inpos] & 0x7F;

            inpos ++;
            outpos ++;
        }
        else if((data[inpos] & 0xE0) == 0xC0)
        {
            chars[outpos] = ((data[inpos] & 0x1F) << 6) | (data[inpos + 1] & 0x3F);

            inpos += 2;
            outpos ++;
        }
        else if((data[inpos] & 0xF0) == 0xE0)
        {
            chars[outpos] = ((data[inpos] & 0x0F) << 12) | ((data[inpos + 1] & 0x3F) << 6) | (data[inpos + 2] & 0x3F);

            inpos += 3;
            outpos ++;
        }
        else if((data[inpos] & 0xF1) == 0xF0)
        {
            chars[outpos] = ((data[inpos] & 0x03) << 18) | ((data[inpos + 1] & 0x3F) << 12) | ((data[inpos + 2] & 0x3F) << 6) | (data[inpos + 3] & 0x3F);

            inpos += 4;
            outpos ++;
        }
        else
        {
            // Error!
            break;
        }
    }

    return chars;
}
