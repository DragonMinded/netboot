#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/reent.h>
#include <stdlib.h>
#include "naomi/system.h"
#include "naomi/maple.h"
#include "naomi/timer.h"

#define CCR (*(uint32_t *)0xFF00001C)
#define QACR0 (*(uint32_t *)0xFF000038)
#define QACR1 (*(uint32_t *)0xFF00003C)

int errno;

/* This is used by _sbrk.  */
register char *stack_ptr asm("r15");

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
    // crt0.s which comes from the entrypoint used to start the code.
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
    // value must be 32 bits. TODO: The data must not also cross any memory boundary
    // that would change address bits 28:26. This could be fixed by detecting such
    // a possibility and updating QACR0/1 during the copy loop.

    // Set the base queue address pointer to the queue location with address bits
    // 25:5. The bottom bits should be all 0s since hw_memset requires an alignment
    // to a 32 byte boundary. We will use both queue areas since SQ0/SQ1 specification
    // is the same bit as address bit 5. Technically this means the below queue setup
    // interleaves the data between the two queues, but it really does not matter what
    // order the hardware copies things.
    uint32_t *queue = (uint32_t *)(STORE_QUEUE_BASE | (((uint32_t)addr) & 0x03FFFFE0));

    // Set the top address bits (28:26) into the store queue address control registers.
    QACR0 = (((uint32_t)addr) >> 24) & 0x1C;
    QACR1 = (((uint32_t)addr) >> 24) & 0x1C;

    // Now, set up both store queues to contain the same value that we want to memset.
    // This is 8 32-bit values per store queue.
    for (int i = 0; i < 16; i++) {
        queue[i] = value;
    }

    // Now, trigger the hardware to copy the values from the queue to the address we
    // care about, triggering one 32-byte prefetch at a time.
    for (int cycles = amount >> 5; cycles > 0; cycles--)
    {
        __asm__("pref @%0" : : "r"(queue));
        queue += 8;
    }

    // Finally, attempt a new write to both queues in order to stall the CPU until the
    // last write is done.
    queue = (uint32_t *)STORE_QUEUE_BASE;
    queue[0] = 0;
    queue[8] = 0;
}

_ssize_t _read_r(struct _reent *reent, int file, void *ptr, size_t len)
{
    // TODO
    return -1;
}

_off_t _lseek_r(struct _reent *reent, int file, _off_t amount, int dir)
{
    // TODO
    return -1;
}

_ssize_t _write_r(struct _reent *reent, int file, const void * ptr, size_t len)
{
    // TODO
    return -1;
}

int _close_r(struct _reent *reent, int file)
{
    // TODO
    return -1;
}

int _link_r(struct _reent *reent, const char *old, const char *new)
{
    // TODO
    return -1;
}

int _rename_r (struct _reent *reent, const char *old, const char *new)
{
    // TODO
    return -1;
}

void *_sbrk_r(struct _reent *reent, ptrdiff_t incr)
{
    // TODO: This is not re-entrant
    extern char end;      /* Defined by the linker */
    static char *heap_end;
    char *prev_heap_end;

    if(heap_end == 0)
    {
        heap_end = &end;
    }
    prev_heap_end = heap_end;
    if(heap_end + incr > stack_ptr)
    {
        _write_r(reent, 1, "Heap and stack collision\n", 25);
        abort();
    }
    heap_end += incr;
    return prev_heap_end;
}

int _fstat_r(struct _reent *reent, int file, struct stat *st)
{
    // TODO
    return -1;
}

int _mkdir_r(struct _reent *reent, const char *path, int flags)
{
    // TODO
    return -1;
}

int _open_r(struct _reent *reent, const char *path, int flags, int unk)
{
    // TODO
    return -1;
}

int _unlink_r(struct _reent *reent, const char *path)
{
    // TODO
    return -1;
}

int _isatty_r(struct _reent *reent, int fd)
{
    // TODO
    return -1;
}

int _kill_r(struct _reent *reent, int n, int m)
{
    // TODO
    return -1;
}

int _getpid_r(struct _reent *reent)
{
    // TODO
    return -1;
}

int _stat_r(struct _reent *reent, const char *path, struct stat *st)
{
    // TODO
    return -1;
}

int _fork_r(struct _reent *reent)
{
    // TODO
    return -1;
}

int _wait_r(struct _reent *reent, int *statusp)
{
    // TODO
    return -1;
}

int _execve_r(struct _reent *reent, const char *path, char *const argv[], char *const envp[])
{
    // TODO
    return -1;
}

_CLOCK_T_ _times_r(struct _reent *reent, struct tms *tm)
{
    // TODO
    return -1;
}

int _gettimeofday_r(struct _reent *reent, struct timeval *tv, void *tz)
{
    // TODO
    tv->tv_usec = 0;
    tv->tv_sec = 0;
    return 0;
}
