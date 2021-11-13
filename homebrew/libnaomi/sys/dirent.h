#ifndef __DIRENT_LIBNAOMI_REPLACEMENT_H
#define __DIRENT_LIBNAOMI_REPLACEMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define NAME_MAX 255

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
    int d_type;
    char d_name[NAME_MAX + 1];
    // TODO: Depending on what software is being ported, we might need
    // to add a few more placeholder values here so it will compile.
};

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
