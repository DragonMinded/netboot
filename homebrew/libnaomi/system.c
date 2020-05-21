#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/reent.h>
#include <stdlib.h>

int errno;

/* This is used by _sbrk.  */
register char *stack_ptr asm("r15");

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
