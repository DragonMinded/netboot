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

#define CCR (*(uint32_t *)0xFF00001C)
#define QACR0 (*(uint32_t *)0xFF000038)
#define QACR1 (*(uint32_t *)0xFF00003C)

#define SYSCALL_VECTOR_BASE ((uint32_t *)0xAC018000)
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

    // Run init sections.
    uint32_t *ctor_ptr = &__ctors;
    while (ctor_ptr < &__ctors_end)
    {
        (*((func_ptr *)ctor_ptr))();
        ctor_ptr++;
    }

    // Execute main/test executable based on boot variable set in
    // sh-crt0.s which comes from the entrypoint used to start the code.
    if(_boot_mode == 0)
    {
        int status;

        timer_init();
        maple_init();
        status = main();
        maple_free();
        timer_free();

        _exit(status);
    }
    else
    {
        int status;

        timer_init();
        maple_init();
        status = test();
        maple_free();
        timer_free();

        _exit(status);
    }
}

void hw_memset(void *addr, uint32_t value, unsigned int amount)
{
    // Very similar to a standard memset, but the address pointer must be aligned
    // to a 32 byte boundary, the amount must be a multiple of 32 bytes and the
    // value must be 32 bits. No checking of these constraints is done.

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
}

void hw_memcpy(void *dest, void *src, unsigned int amount)
{
    // Very similar to a standard memcpy, but the destination pointer must be aligned
    // to a 32 byte boundary, the amount must be a multiple of 32 bytes and the source
    // pointer must be aligned to a 4 byte boundary. No checking of these constraints
    // is done.

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
}

void enter_test_mode()
{
    // Look up the address of the enter test mode syscall.
    uint32_t test_mode_syscall = SYSCALL_VECTOR_BASE[SYSCALL_ENTER_TEST_MODE] | UNCACHED_MIRROR;

    // Call it.
    ((void (*)())test_mode_syscall)();
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
        // TODO: Implement read once we have FS support.
        reent->_errno = ENOTSUP;
        return -1;
    }
}

_off_t _lseek_r(struct _reent *reent, int file, _off_t amount, int dir)
{
    // TODO: Implement seek once we have FS support.
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
        // TODO: Implement write once we have FS support.
        reent->_errno = ENOTSUP;
        return -1;
    }
}

int _close_r(struct _reent *reent, int file)
{
    // TODO: Implement close once we have FS support.
    reent->_errno = ENOTSUP;
    return -1;
}

int _link_r(struct _reent *reent, const char *old, const char *new)
{
    // TODO: Implement link once we have FS support.
    reent->_errno = ENOTSUP;
    return -1;
}

int _rename_r (struct _reent *reent, const char *old, const char *new)
{
    // TODO: Implement rename once we have FS support.
    reent->_errno = ENOTSUP;
    return -1;
}

void *_sbrk_r(struct _reent *reent, ptrdiff_t incr)
{
    // TODO: This is not re-entrant, but it's not possible to make so. We need
    // to disable and re-enable interrupts here, but we don't support interrupts yet.

    extern char end;      /* Defined by the linker in naomi.ld */
    static char *heap_end;
    char *prev_heap_end;
    register char *stack_ptr asm("r15");

    if(heap_end == 0)
    {
        heap_end = &end;
    }
    prev_heap_end = heap_end;
    if(heap_end + incr > stack_ptr)
    {
        reent->_errno = ENOMEM;
        return (void *)-1;
    }
    heap_end += incr;
    return prev_heap_end;
}

int _fstat_r(struct _reent *reent, int file, struct stat *st)
{
    // TODO: Implement fstat once we have FS support.
    reent->_errno = ENOTSUP;
    return -1;
}

int _mkdir_r(struct _reent *reent, const char *path, int flags)
{
    // TODO: Implement mkdir once we have FS support.
    reent->_errno = ENOTSUP;
    return -1;
}

int _open_r(struct _reent *reent, const char *path, int flags, int unk)
{
    // TODO: Implement open once we have FS support.
    reent->_errno = ENOTSUP;
    return -1;
}

int _unlink_r(struct _reent *reent, const char *path)
{
    // TODO: Implement unlink once we have FS support.
    reent->_errno = ENOTSUP;
    return -1;
}

int _isatty_r(struct _reent *reent, int fd)
{
    // TODO: Implement isatty once we have some sort of console bridge support.
    reent->_errno = ENOTSUP;
    return -1;
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
    // TODO: Implement stat once we have FS support.
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
