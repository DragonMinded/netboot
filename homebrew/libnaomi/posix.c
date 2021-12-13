#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/reent.h>
#include <sys/errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include "naomi/interrupt.h"
#include "naomi/posix.h"
#include "naomi/rtc.h"
#include "naomi/thread.h"
#include "irqstate.h"

/* Actual definition of global errno */
int errno;

/* C++ expected definitions */
void *__dso_handle = NULL;

/* stdio hook mutex */
static mutex_t stdio_mutex;

/* Forward definitions for some hook functions. */
void _fs_init();
void _fs_free();

void _posix_init()
{
    mutex_init(&stdio_mutex);

    _fs_init();
}

void _posix_free()
{
    _fs_free();

    // We intentionally don't kill the mutex here because we want
    // it to protect until the point where threads are torn down.
    // When that happens, all mutexes in the system will also be freed.
}

void __assert_func(const char * file, int line, const char *func, const char *failedexpr)
{
    _irq_display_invariant(
        "assertion failure",
        "assertion \"%s\" failed: file \"%s\", line %d%s%s\n",
        failedexpr,
        file,
        line,
        func ? ", function: " : "", func ? func : ""
    );
}

// Currently hooked stdio calls.
typedef struct stdio_registered_hooks
{
    stdio_t stdio_hooks;
    struct stdio_registered_hooks *next;
} stdio_registered_hooks_t;

stdio_registered_hooks_t *stdio_hooks = 0;

void * hook_stdio_calls( stdio_t *stdio_calls )
{
    if( stdio_calls == NULL )
    {
        /* Failed to hook, bad input */
        return 0;
    }

    /* Safe to hook */
    stdio_registered_hooks_t *new_hooks = malloc(sizeof(stdio_registered_hooks_t));
    if (new_hooks == 0)
    {
        _irq_display_invariant("memory failure", "could not get memory for stdio hooks!");
    }
    new_hooks->stdio_hooks.stdin_read = stdio_calls->stdin_read;
    new_hooks->stdio_hooks.stdout_write = stdio_calls->stdout_write;
    new_hooks->stdio_hooks.stderr_write = stdio_calls->stderr_write;

    /* Make sure another thread doesn't try to access our structure
     * while we're doing this. */
    mutex_lock(&stdio_mutex);

    /* Add it to the list. */
    new_hooks->next = stdio_hooks;
    stdio_hooks = new_hooks;

    /* Safe to use again. */
    mutex_unlock(&stdio_mutex);

    /* Success */
    return (void *)new_hooks;
}

int unhook_stdio_calls( void *prevhook )
{
    int retval = -1;

    mutex_lock(&stdio_mutex);
    if (prevhook != NULL)
    {
        if (stdio_hooks == prevhook)
        {
            stdio_hooks = stdio_hooks->next;
            free(prevhook);
            retval = 0;
        }
        else
        {
            stdio_registered_hooks_t *curhooks = stdio_hooks;
            while(curhooks != 0)
            {
                if (curhooks->next == prevhook)
                {
                    curhooks->next = curhooks->next->next;
                    free(prevhook);
                    retval = 0;
                    break;
                }

                /* Didn't find it, try the next one. */
                curhooks = curhooks->next;
            }
        }
    }

    /* Return 0 if we succeeded, -1 if we couldn't find the hooks. */
    mutex_unlock(&stdio_mutex);
    return retval;
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
    /* How many copies of ourselves exist. */
    int copies;
} fs_handle_t;

static fs_mapping_t filesystems[MAX_FILESYSTEMS];
static fs_handle_t handles[MAX_OPEN_FILES];

void _fs_init()
{
    uint32_t old_irq = irq_disable();
    memset(filesystems, 0, sizeof(fs_mapping_t) * MAX_FILESYSTEMS);
    memset(handles, 0, sizeof(fs_handle_t) * MAX_OPEN_FILES);
    irq_restore(old_irq);
}

void _fs_free()
{
    uint32_t old_irq = irq_disable();

    // Go through and close all open file handles for all filesystems.
    for (int j = 0; j < MAX_OPEN_FILES; j++)
    {
        if (handles[j].fileno > 0 && handles[j].handle != 0)
        {
            filesystems[handles[j].fs_mapping].fs->close(filesystems[handles[j].fs_mapping].fshandle, handles[j].handle);
        }
    }

    memset(filesystems, 0, sizeof(fs_mapping_t) * MAX_FILESYSTEMS);
    memset(handles, 0, sizeof(fs_handle_t) * MAX_OPEN_FILES);
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
                for (int j = 0; j < MAX_OPEN_FILES; j++)
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

int _fs_next_free_handle()
{
    /* Start past STDIN, STDOUT, STDERR file handles */
    static int handle = 3;

    /* The handle we're about to give back. If there aren't free handles, return -1. */
    int newhandle = -1;

    /* Make sure we don't screw up and give the same file handle to multiple threads. */
    uint32_t old_irq = irq_disable();
    for (unsigned int j = 0; j < MAX_OPEN_FILES; j++)
    {
        unsigned int slot = (handle + j) % MAX_OPEN_FILES;

        if (handles[slot].fileno == 0)
        {
            /* Consume and then return this handle. */
            newhandle = handle + j;
            handle = newhandle + 1;
            break;
        }
    }

    /* Return either the handle we found, or -1 to indicate no more free files. */
    irq_restore(old_irq);
    return newhandle;
}

int dup(int oldfile)
{
    // Make sure to copy everything atomically.
    uint32_t old_irq = irq_disable();
    int newfile = -1;

    if (handles[oldfile % MAX_OPEN_FILES].fileno == oldfile)
    {
        int oldoffset = oldfile % MAX_OPEN_FILES;

        // Duplicate a file handle, returning a new handle.
        newfile = _fs_next_free_handle();
        if (newfile < 0)
        {
            errno = EMFILE;
            newfile = -1;
        }
        else
        {
            // We have both the old file existing, and a new hanle for it.
            int newoffset = newfile % MAX_OPEN_FILES;

            // Set up the new file.
            handles[newoffset].fileno = newfile;
            handles[newoffset].handle = handles[oldoffset].handle;
            handles[newoffset].fs_mapping = handles[oldoffset].fs_mapping;
            handles[newoffset].copies = handles[oldoffset].copies;

            // Set the copies plus 1 to all copies of this file handle.
            handles[oldoffset].copies++;
            for (int j = 0; j < MAX_OPEN_FILES; j++)
            {
                if (handles[j].handle == handles[oldoffset].handle)
                {
                    handles[j].copies++;
                }
            }
        }
    }
    else
    {
        errno = EBADF;
        newfile = -1;
    }

    irq_restore(old_irq);
    return newfile;
}

FILE * popen(const char *command, const char *type)
{
    // Don't support process open.
    errno = ENOTSUP;
    return 0;
}

int pclose(FILE *stream)
{
    // Don't support process close.
    errno = ENOTSUP;
    return -1;
}

int _fs_get_hooks(int fileno, filesystem_t **fs, void **fshandle, void **handle)
{
    if( fileno < 3 )
    {
        return 0;
    }

    int slot = fileno % MAX_OPEN_FILES;
    if (handles[slot].fileno == fileno)
    {
        // Found it!
        *fs = filesystems[handles[slot].fs_mapping].fs;
        *fshandle = filesystems[handles[slot].fs_mapping].fshandle;
        *handle = handles[slot].handle;
        return 1;
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

char *realpath(const char *restrict path, char *restrict resolved_path)
{
    if (path == 0)
    {
        errno = EINVAL;
        return 0;
    }

    int mapping = _fs_get_fs_by_name(path);
    if (mapping >= 0)
    {
        const char *fullpath = path + strlen(filesystems[mapping].prefix);

        if (fullpath[0] != '/')
        {
            // Paths MUST be absolute, we do not support chdir()!
            errno = ENOENT;
            return 0;
        }
        // Skip past leading '/'.
        fullpath ++;

        // We need some memory for resolved_path if its not provided.
        int allocated = 0;
        if (resolved_path == 0)
        {
            resolved_path = malloc(PATH_MAX + 1);
            if (resolved_path == 0)
            {
                errno = ENOMEM;
                return 0;
            }
            allocated = 1;
        }

        if (fullpath[0] != 0)
        {
            char **parts = malloc(sizeof(char **));
            if (parts == 0)
            {
                if (allocated)
                {
                    free(resolved_path);
                }
                errno = ENOMEM;
                return 0;
            }
            parts[0] = malloc(PATH_MAX + 1);
            if (parts[0] == 0)
            {
                if (allocated)
                {
                    free(resolved_path);
                }
                free(parts);
                errno = ENOMEM;
                return 0;
            }
            memset(parts[0], 0, PATH_MAX + 1);
            int partscount = 1;
            int partpos = 0;
            int trailing_slash = 0;

            // Separate out into parts.
            while (fullpath[0] != 0)
            {
                if (fullpath[0] == '/')
                {
                    if (fullpath[1] == 0)
                    {
                        // Don't need a new allocation, we're good as-is.
                        trailing_slash = 1;
                        break;
                    }
                    else
                    {
                        // Need to allocate more for parts.
                        partscount++;
                        parts = realloc(parts, sizeof(char **) * partscount);
                        parts[partscount - 1] = malloc(PATH_MAX + 1);
                        if (parts[partscount - 1] == 0)
                        {
                            if (allocated)
                            {
                                free(resolved_path);
                            }
                            for (int i = 0; i < partscount - 1; i++)
                            {
                                free(parts[i]);
                            }
                            free(parts);
                            errno = ENOMEM;
                            return 0;
                        }
                        memset(parts[partscount - 1], 0, PATH_MAX + 1);
                        partpos = 0;
                    }

                    // Skip past this character.
                    fullpath++;
                }
                else
                {
                    parts[partscount - 1][partpos] = fullpath[0];
                    partpos++;
                    fullpath++;
                }
            }

            // At this point we have a string of path parts. Now we need to rejoin
            // them all canonicalized. First, take care of . and .. in the path.
            char **newparts = malloc(sizeof(char **) * partscount);
            if (newparts == 0)
            {
                if (allocated)
                {
                    free(resolved_path);
                }
                for (int i = 0; i < partscount; i++)
                {
                    free(parts[i]);
                }
                free(parts);
                errno = ENOMEM;
                return 0;
            }
            int newpartscount = 0;
            for (int i = 0; i < partscount; i++)
            {
                if (parts[i][0] == 0 || strcmp(parts[i], ".") == 0)
                {
                    // Ignore it, its just pointing at the current directory.
                }
                else if (strcmp(parts[i], "..") == 0)
                {
                    // Pop one directory.
                    if (newpartscount > 0)
                    {
                        newpartscount--;
                    }
                }
                else
                {
                    // Push one directory.
                    newparts[newpartscount] = parts[i];
                    newpartscount++;
                }
            }

            // Now, we must go through and make sure each part of the canonical path
            // is actually a directory.
            int all_okay = 1;
            strcpy(resolved_path, filesystems[mapping].prefix);
            strcat(resolved_path, "/");

            for (int i = 0; i < newpartscount; i++)
            {
                // First, concatenate it onto the path.
                if (resolved_path[strlen(resolved_path) - 1] != '/')
                {
                    strcat(resolved_path, "/");
                }
                strcat(resolved_path, newparts[i]);

                // Second, make sure it is a directory. It can only be a file if it
                // is the last entry in the path.
                struct stat st;
                if (stat(resolved_path, &st) != 0)
                {
                    // We leave the errno alone so it can be returned.
                    all_okay = 0;
                    break;
                }

                if ((st.st_mode & S_IFDIR) != 0)
                {
                    // Its a directory!
                    if (i == (newpartscount - 1))
                    {
                        // Need to append a final '/'.
                        strcat(resolved_path, "/");
                    }
                }
                else if ((st.st_mode & S_IFREG) != 0)
                {
                    // It can only be a file if it is the last part.
                    if (i != (newpartscount - 1))
                    {
                        errno = ENOTDIR;
                        all_okay = 0;
                        break;
                    }
                    else if(trailing_slash)
                    {
                        errno = ENOTDIR;
                        all_okay = 0;
                        break;
                    }
                }
                else
                {
                    // Unclear what this is, not valid.
                    errno = ENOTDIR;
                    all_okay = 0;
                    break;
                }
            }

            // Now, free up memory.
            for (int i = 0; i < partscount; i++)
            {
                free(parts[i]);
            }
            free(parts);
            free(newparts);

            // Finally, return it if it was okay.
            if (all_okay)
            {
                return resolved_path;
            }
            else
            {
                if (allocated)
                {
                    free(resolved_path);
                }

                return 0;
            }
        }
        else
        {
            // Path is already normalized root path.
            strcpy(resolved_path, path);
            return resolved_path;
        }
    }
    else
    {
        errno = ENOENT;
        return 0;
    }
}

_ssize_t _read_r(struct _reent *reent, int file, void *ptr, size_t len)
{
    if( file == 0 )
    {
        /* If we don't get a valid hook, then this wasn't supported. */
        int retval = -ENOTSUP;

        /* Only read from the first valid hooks. */
        mutex_lock(&stdio_mutex);
        stdio_registered_hooks_t *curhook = stdio_hooks;
        while (curhook != 0)
        {
            if (curhook->stdio_hooks.stdin_read)
            {
                retval = curhook->stdio_hooks.stdin_read( ptr, len );
                break;
            }

            curhook = curhook->next;
        }

        mutex_unlock(&stdio_mutex);

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
        /* If we don't get a valid hook, then this wasn't supported. */
        int retval = -ENOTSUP;

        /* Write to every single valid hook. Ignore returns. */
        mutex_lock(&stdio_mutex);
        stdio_registered_hooks_t *curhook = stdio_hooks;
        while (curhook != 0)
        {
            if (curhook->stdio_hooks.stdout_write)
            {
                curhook->stdio_hooks.stdout_write( ptr, len );
                retval = len;
            }

            curhook = curhook->next;
        }
        mutex_unlock(&stdio_mutex);

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
    else if( file == 2 )
    {
        /* If we don't get a valid hook, then this wasn't supported. */
        int retval = -ENOTSUP;

        /* Write to every single valid hook. Ignore returns. */
        mutex_lock(&stdio_mutex);
        stdio_registered_hooks_t *curhook = stdio_hooks;
        while (curhook != 0)
        {
            if (curhook->stdio_hooks.stderr_write)
            {
                curhook->stdio_hooks.stderr_write( ptr, len );
                retval = len;
            }

            curhook = curhook->next;
        }
        mutex_unlock(&stdio_mutex);

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
        /* First, figure out if we need to close this handle at all, or if
         * we have some duplicates hanging around. */
        int copies = handles[file % MAX_OPEN_FILES].copies;

        int retval;
        if (fs->close == 0)
        {
            /* Filesystem doesn't support close */
            retval = -ENOTSUP;
        }
        else
        {
            if (copies == 1)
            {
                /* Perform the close action. */
                retval = fs->close(fshandle, handle);
            }
            else
            {
                /* Don't actually close the file, we have more than one handle around. */
                retval = 0;
            }
        }

        /* Finally, before we return, unregister this handle. */
        for( int i = 0; i < MAX_OPEN_FILES; i++)
        {
            if (handles[i].handle == handle)
            {
                handles[i].copies --;
                if (handles[i].copies == 0)
                {
                    memset(&handles[i], 0, sizeof(fs_handle_t));
                }
            }
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
    int newhandle = _fs_next_free_handle();
    if (newhandle < 0)
    {
        /* No file handles available */
        reent->_errno = ENFILE;
        return -1;
    }
    else
    {
        /* Yes, we have room, try the open */
        int slot = newhandle % MAX_OPEN_FILES;
        void *ptr = fs->open(filesystems[mapping].fshandle, path + strlen(filesystems[mapping].prefix), flags, mode);
        int errnoptr = (int)ptr;

        if (errnoptr > 0)
        {
            /* Create new internal handle */
            handles[slot].fileno = newhandle;
            handles[slot].handle = ptr;
            handles[slot].fs_mapping = mapping;
            handles[slot].copies = 1;

            /* Return our own handle */
            return handles[slot].fileno;
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
    // We have threads but no processes, so let's not pretend with half support.
    reent->_errno = ENOTSUP;
    return -1;
}

int _getpid_r(struct _reent *reent)
{
    // We have threads but no processes, so let's not pretend with half support.
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
            if (-handleint == EISDIR)
            {
                /* This is actually a directory, not a file. */
                memset(st, 0, sizeof(struct stat));
                st->st_mode = S_IFDIR;
                st->st_nlink = 1;
                return 0;
            }
            else
            {
                reent->_errno = -handleint;
                return -1;
            }
        }
    }

    /* There is no filesystem backing this file. */
    reent->_errno = ENOTSUP;
    return -1;
}

int _fork_r(struct _reent *reent)
{
    // We have threads but no processes, so let's not pretend with half support.
    reent->_errno = ENOTSUP;
    return -1;
}

int _wait_r(struct _reent *reent, int *statusp)
{
    // We have threads but no processes, so let's not pretend with half support.
    reent->_errno = ENOTSUP;
    return -1;
}

int _execve_r(struct _reent *reent, const char *path, char *const argv[], char *const envp[])
{
    // We have threads but no processes, so let's not pretend with half support.
    reent->_errno = ENOTSUP;
    return -1;
}

_CLOCK_T_ _times_r(struct _reent *reent, struct tms *tm)
{
    // We have threads but no processes, so let's not pretend with half support.
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
    uint32_t old_irq = irq_disable();

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
    else
    {
        // Technically this should do nothing, but at least it is symmetrical.
        irq_restore(old_irq);
    }
}

DIR *opendir(const char *name)
{
    int mapping = _fs_get_fs_by_name(name);
    if (mapping >= 0)
    {
        filesystem_t *fs = filesystems[mapping].fs;
        if (fs->opendir == 0)
        {
            /* Filesystem doesn't support opendir */
            errno = ENOTSUP;
            return 0;
        }

        void *handle = fs->opendir(filesystems[mapping].fshandle, name + strlen(filesystems[mapping].prefix));
        int errnohandle = (int)handle;

        if (errnohandle > 0)
        {
            /* Create new DIR handle */
            DIR *dir = malloc(sizeof(DIR));
            if (dir == 0)
            {
                if (fs->closedir)
                {
                    fs->closedir(filesystems[mapping].fshandle, handle);
                }
                errno = ENOMEM;
                return 0;
            }
            dir->handle = handle;
            dir->fs = mapping;
            dir->ent = malloc(sizeof(struct dirent));
            if (dir->ent == 0)
            {
                if (fs->closedir)
                {
                    fs->closedir(filesystems[mapping].fshandle, handle);
                }
                free(dir);
                errno = ENOMEM;
                return 0;
            }
            else
            {
                return dir;
            }
        }
        else
        {
            /* Couldn't open for some reason */
            if (errnohandle == 0)
            {
                errno = ENOENT;
            }
            else
            {
                errno = -errnohandle;
            }
            return 0;
        }
    }

    /* We don't have a filesystem mapping for this file. */
    errno = ENOTSUP;
    return 0;
}

struct dirent *readdir(DIR *dirp)
{
    if (dirp == 0)
    {
        errno = EINVAL;
        return 0;
    }
    else
    {
        if (dirp->fs >= 0)
        {
            filesystem_t *fs = filesystems[dirp->fs].fs;
            if (fs->readdir == 0)
            {
                /* Filesystem doesn't support readdir */
                errno = ENOTSUP;
                return 0;
            }

            int retval = fs->readdir(filesystems[dirp->fs].fshandle, dirp->handle, dirp->ent);
            if (retval < 0)
            {
                errno = -retval;
                return 0;
            }
            else if(retval > 0)
            {
                return dirp->ent;
            }
            else
            {
                return 0;
            }
        }
        else
        {
            /* Somehow gave us a bogus DIR structure. */
            errno = ENOTSUP;
            return 0;
        }
    }
}

void seekdir(DIR *dirp, long loc)
{
    if (dirp == 0)
    {
        return;
    }
    else
    {
        if (dirp->fs >= 0)
        {
            filesystem_t *fs = filesystems[dirp->fs].fs;
            if (fs->seekdir == 0)
            {
                /* Filesystem doesn't support seekdir/telldir */
                return;
            }

            fs->seekdir(filesystems[dirp->fs].fshandle, dirp->handle, loc);
        }
    }
}

long telldir(DIR *dirp)
{
    if (dirp == 0)
    {
        errno = EINVAL;
        return -1;
    }
    else
    {
        if (dirp->fs >= 0)
        {
            filesystem_t *fs = filesystems[dirp->fs].fs;
            if (fs->seekdir == 0)
            {
                /* Filesystem doesn't support seekdir/telldir */
                errno = ENOTSUP;
                return -1;
            }

            int retval = fs->seekdir(filesystems[dirp->fs].fshandle, dirp->handle, -1);
            if (retval < 0)
            {
                errno = -retval;
                return -1;
            }
            else
            {
                return retval;
            }
        }
        else
        {
            /* Somehow gave us a bogus DIR structure. */
            errno = ENOTSUP;
            return -1;
        }
    }
}

int closedir(DIR *dirp)
{
    if (dirp == 0)
    {
        errno = EINVAL;
        return -1;
    }
    else
    {
        if (dirp->fs >= 0)
        {
            filesystem_t *fs = filesystems[dirp->fs].fs;
            if (fs->closedir == 0)
            {
                /* Filesystem doesn't support closedir */
                errno = ENOTSUP;
                return -1;
            }

            int retval = fs->closedir(filesystems[dirp->fs].fshandle, dirp->handle);
            free(dirp->ent);
            free(dirp);

            if (retval < 0)
            {
                errno = -retval;
                return -1;
            }
            else
            {
                return retval;
            }
        }
        else
        {
            /* Somehow gave us a bogus DIR structure. */
            errno = ENOTSUP;
            return -1;
        }
    }
}
