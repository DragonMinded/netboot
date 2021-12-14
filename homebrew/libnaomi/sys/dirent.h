#ifndef __DIRENT_LIBNAOMI_REPLACEMENT_H
#define __DIRENT_LIBNAOMI_REPLACEMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define NAME_MAX 255
#define PATH_MAX 2047

#define DT_UNKNOWN 0
#define DT_REG 1
#define DT_DIR 2
#define DT_FIFO 3
#define DT_SOCK 4
#define DT_CHR 5
#define DT_BLK 6
#define DT_LNK 7

typedef struct dirent
{
    ino_t d_ino;
    char d_name[NAME_MAX + 1];
    // The following are not part of the POSIX specification but are often
    // used by other software. So, present them here and do our best to
    // fill them in. In particular, d_type is filled in with the entry
    // type (file, directory, etc) as best we can.
    unsigned char d_type;
};

// Advertise that we have the d_type extension.
#define _DIRENT_HAVE_D_TYPE 1

typedef struct
{
    int fs;
    void *handle;
    struct dirent *ent;
} DIR;

int closedir(DIR *dirp);
DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
void seekdir(DIR *dirp, long loc);
long telldir(DIR *dirp);
#ifdef __cplusplus
}
#endif

#endif
