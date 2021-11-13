// vim: set fileencoding=utf-8
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include "naomi/romfs.h"

void test_romfs_simple(test_context_t *context)
{
    ASSERT(romfs_init_default() == 0, "ROMFS init failed!");

    FILE *fp = fopen("rom://test.txt", "r");
    ASSERT(fp != NULL, "ROMFS failed to open root file, errno is \"%s\" (%d)!", strerror(errno), errno);

    ASSERT(ftell(fp) == 0, "ROMFS file in the wrong location!");

    ASSERT(fp != NULL, "ROMFS failed to open root file, errno is \"%s\" (%d)!", strerror(errno), errno);

    ASSERT(ftell(fp) == 0, "ROMFS file in the wrong location!");

    uint8_t buffer[128];
    memset(buffer, 0xFF, 128);
    ASSERT(fread(buffer, 1, 128, fp) == 19, "ROMFS returned wrong read length!");

    ASSERT(memcmp(buffer, "This is test data.\n", 19) == 0, "ROMFS did not read file correctly!");
    for (int i = 19; i < 128; i++)
    {
        ASSERT(buffer[i] == 0xFF, "Buffer contents incorrectly modified at offset %d, %02x != ff!", i, buffer[i]);
    }

    ASSERT(ftell(fp) == 19, "ROMFS file in the wrong location!");

    memset(buffer, 0xFF, 128);
    ASSERT(fread(buffer, 1, 128, fp) == 0, "ROMFS read past the end of the file?");
    for (int i = 0; i < 128; i++)
    {
        ASSERT(buffer[i] == 0xFF, "Buffer contents incorrectly modified at offset %d, %02x != ff!", i, buffer[i]);
    }

    ASSERT(feof(fp), "ROMFS file is not reported to be EOF!");
    ASSERT(fclose(fp) == 0, "ROMFS failed to close file!");

    romfs_free_default();
}

void test_romfs_nonexistent(test_context_t *context)
{
    ASSERT(romfs_init_default() == 0, "ROMFS init failed!");

    FILE *fp = fopen("rom://file.txt", "r");
    ASSERT(fp == NULL, "ROMFS opened nonexistent file!");
    ASSERT(errno == ENOENT, "Got wrong errno \"%s\" (%d) for file open!", strerror(errno), errno);

    fp = fopen("rom://subdir", "r");
    ASSERT(fp == NULL, "ROMFS opened directory as file!");
    ASSERT(errno == EISDIR, "Got wrong errno \"%s\" (%d) for file open!", strerror(errno), errno);

    romfs_free_default();
}

void test_romfs_seek(test_context_t *context)
{
    ASSERT(romfs_init_default() == 0, "ROMFS init failed!");

    FILE *fp = fopen("rom://test.txt", "r");
    ASSERT(fp != NULL, "ROMFS failed to open root file, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT(ftell(fp) == 0, "ROMFS file in the wrong location!");

    // Do a bunch of seeks and make sure we suppor this correctly.
    uint8_t byte = 0xFF;
    ASSERT(fread(&byte, 1, 1, fp) == 1, "ROMFS read more than 1 byte!");
    ASSERT(byte == 'T', "ROMFS returned wrong data %c!", byte);
    ASSERT(ftell(fp) == 1, "ROMFS file in the wrong location!");

    ASSERT(fseek(fp, 13, SEEK_SET) == 0, "ROMFS failed to seek to new location, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT(ftell(fp) == 13, "ROMFS file in the wrong location!");

    ASSERT(fread(&byte, 1, 1, fp) == 1, "ROMFS read more than 1 byte!");
    ASSERT(byte == 'd', "ROMFS returned wrong data %c!", byte);
    ASSERT(ftell(fp) == 14, "ROMFS file in the wrong location!");

    ASSERT(fseek(fp, 3, SEEK_CUR) == 0, "ROMFS failed to seek to new location, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT(ftell(fp) == 17, "ROMFS file in the wrong location!");

    ASSERT(fread(&byte, 1, 1, fp) == 1, "ROMFS read more than 1 byte!");
    ASSERT(byte == '.', "ROMFS returned wrong data %c!", byte);
    ASSERT(ftell(fp) == 18, "ROMFS file in the wrong location!");

    ASSERT(fseek(fp, -11, SEEK_END) == 0, "ROMFS failed to seek to new location, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT(ftell(fp) == 8, "ROMFS file in the wrong location!");

    ASSERT(fread(&byte, 1, 1, fp) == 1, "ROMFS read more than 1 byte!");
    ASSERT(byte == 't', "ROMFS returned wrong data %c!", byte);
    ASSERT(ftell(fp) == 9, "ROMFS file in the wrong location!");

    rewind(fp);
    ASSERT(ftell(fp) == 0, "ROMFS file in the wrong location!");

    // Now check that advancing the file by reading works correctly.
    uint8_t buffer[128];
    memset(buffer, 0xFF, 128);
    int loc = 0;
    while(fread(buffer + loc, 1, 1, fp) == 1)
    {
        loc++;
    }

    ASSERT(loc == 19, "Read the wrong number of bytes from ROMFS!");
    ASSERT(ftell(fp) == 19, "ROMFS file in the wrong location!");

    ASSERT(memcmp(buffer, "This is test data.\n", 19) == 0, "ROMFS did not read file correctly!");
    for (int i = 19; i < 128; i++)
    {
        ASSERT(buffer[i] == 0xFF, "Buffer contents incorrectly modified at offset %d, %02x != ff!", i, buffer[i]);
    }

    ASSERT(fclose(fp) == 0, "ROMFS failed to close file!");

    romfs_free_default();
}

void test_romfs_stat(test_context_t *context)
{
    ASSERT(romfs_init_default() == 0, "ROMFS init failed!");

    int filedes = open("rom://test.txt", O_RDONLY);

    struct stat buffer;
    ASSERT(fstat(filedes, &buffer) == 0, "ROMFS fstat call failed, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT((buffer.st_mode & S_IFREG) == S_IFREG, "ROMFS fstat call returned invalid mode %04lx", buffer.st_mode);
    ASSERT(buffer.st_nlink == 1, "ROMFS fstat call returned invalid number of links %d", buffer.st_nlink);
    ASSERT(buffer.st_size == 19, "ROMFS fstat call returned invalid file size %ld", buffer.st_size);

    close(filedes);

    struct stat buffer2;
    ASSERT(stat("rom://test.txt", &buffer2) == 0, "ROMFS fstat call failed, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT((buffer2.st_mode & S_IFREG) == S_IFREG, "ROMFS fstat call returned invalid mode %04lx", buffer2.st_mode);
    ASSERT(buffer2.st_nlink == 1, "ROMFS fstat call returned invalid number of links %d", buffer2.st_nlink);
    ASSERT(buffer2.st_size == 19, "ROMFS fstat call returned invalid file size %ld", buffer2.st_size);

    ASSERT(stat("rom://missing.txt", &buffer2) == -1, "ROMFS fstat call succeeded unexpectedly!");
    ASSERT(errno == ENOENT, "ROMFS errno wrong, errno returned is \"%s\" (%d)!", strerror(errno), errno);

    struct stat buffer3;
    ASSERT(stat("rom://subdir", &buffer3) == 0, "ROMFS fstat call failed, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT((buffer3.st_mode & S_IFDIR) == S_IFDIR, "ROMFS fstat call returned invalid mode %04lx", buffer3.st_mode);
    ASSERT(buffer3.st_nlink == 1, "ROMFS fstat call returned invalid number of links %d", buffer3.st_nlink);

    ASSERT(stat("rom://subdir/", &buffer3) == 0, "ROMFS fstat call failed, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT((buffer3.st_mode & S_IFDIR) == S_IFDIR, "ROMFS fstat call returned invalid mode %04lx", buffer3.st_mode);
    ASSERT(buffer3.st_nlink == 1, "ROMFS fstat call returned invalid number of links %d", buffer3.st_nlink);

    romfs_free_default();
}

void test_romfs_traversal(test_context_t *context)
{
    char buffer[128];
    FILE *fp;

    ASSERT(romfs_init_default() == 0, "ROMFS init failed!");

    // Test that we can locate a file in a subdirectory.
    fp = fopen("rom://subdir/test.txt", "r");
    memset(buffer, 0x0, 128);
    ASSERT(fread(buffer, 1, 128, fp) == 20, "ROMFS returned wrong read length!");
    ASSERT(strcmp(buffer, "This is other data!\n") == 0, "ROMFS returned data from wrong file!");
    fclose(fp);

    // Test that we do not find a file that is in the root when we go into a subdirectory.
    fp = fopen("rom://empty_dir/test.txt", "r");
    ASSERT(fp == 0, "ROMFS unexpectedly opened file!");

    // Test that we can traverse to our own directory as many times as needed.
    fp = fopen("rom://./subdir/test.txt", "r");
    memset(buffer, 0x0, 128);
    ASSERT(fread(buffer, 1, 128, fp) == 20, "ROMFS returned wrong read length!");
    ASSERT(strcmp(buffer, "This is other data!\n") == 0, "ROMFS returned data from wrong file!");
    fclose(fp);

    fp = fopen("rom://./test.txt", "r");
    memset(buffer, 0x0, 128);
    ASSERT(fread(buffer, 1, 128, fp) == 19, "ROMFS returned wrong read length!");
    ASSERT(strcmp(buffer, "This is test data.\n") == 0, "ROMFS returned data from wrong file!");
    fclose(fp);

    fp = fopen("rom://./subdir/././././test.txt", "r");
    memset(buffer, 0x0, 128);
    ASSERT(fread(buffer, 1, 128, fp) == 20, "ROMFS returned wrong read length!");
    ASSERT(strcmp(buffer, "This is other data!\n") == 0, "ROMFS returned data from wrong file!");
    fclose(fp);

    fp = fopen("rom://./subdir/././////./test.txt", "r");
    memset(buffer, 0x0, 128);
    ASSERT(fread(buffer, 1, 128, fp) == 20, "ROMFS returned wrong read length!");
    ASSERT(strcmp(buffer, "This is other data!\n") == 0, "ROMFS returned data from wrong file!");
    fclose(fp);

    // Test that we can traverse back up the directory tree and get files correctly.
    fp = fopen("rom://empty_dir/../subdir/../test.txt", "r");
    memset(buffer, 0x0, 128);
    ASSERT(fread(buffer, 1, 128, fp) == 19, "ROMFS returned wrong read length!");
    ASSERT(strcmp(buffer, "This is test data.\n") == 0, "ROMFS returned data from wrong file!");
    fclose(fp);

    // Test that we can go up in the root directory and still get the root directory.
    fp = fopen("rom://../../../test.txt", "r");
    memset(buffer, 0x0, 128);
    ASSERT(fread(buffer, 1, 128, fp) == 19, "ROMFS returned wrong read length!");
    ASSERT(strcmp(buffer, "This is test data.\n") == 0, "ROMFS returned data from wrong file!");
    fclose(fp);

    fp = fopen("rom://../../../subdir/test.txt", "r");
    memset(buffer, 0x0, 128);
    ASSERT(fread(buffer, 1, 128, fp) == 20, "ROMFS returned wrong read length!");
    ASSERT(strcmp(buffer, "This is other data!\n") == 0, "ROMFS returned data from wrong file!");
    fclose(fp);

    romfs_free_default();
}

void test_romfs_directory(test_context_t *context)
{
    ASSERT(romfs_init_default() == 0, "ROMFS init failed!");

    DIR *dirp;
    int file_count;

    // Test the root directory.
    dirp = opendir("rom://");
    ASSERT(dirp != 0, "Failed to open directory in ROMFS, errno is \"%s\" (%d)!", strerror(errno), errno);

    file_count = 0;
    while (1)
    {
        errno = 0;
        struct dirent* direntp = readdir( dirp );
        if (direntp == NULL)
        {
            ASSERT(errno == 0, "Got error return from readdir, errno is \"%s\" (%d)!", strerror(errno), errno);
            break;
        }
        file_count++;

        if (strcmp(direntp->d_name, ".") == 0)
        {
            ASSERT(direntp->d_type == DT_DIR, "Expected %s to be %d but got %d", direntp->d_name, DT_DIR, direntp->d_type);
            continue;
        }
        if (strcmp(direntp->d_name, "..") == 0)
        {
            ASSERT(direntp->d_type == DT_DIR, "Expected %s to be %d but got %d", direntp->d_name, DT_DIR, direntp->d_type);
            continue;
        }
        if (strcmp(direntp->d_name, "test.txt") == 0)
        {
            ASSERT(direntp->d_type == DT_REG, "Expected %s to be %d but got %d", direntp->d_name, DT_REG, direntp->d_type);
            continue;
        }
        if (strcmp(direntp->d_name, "subdir") == 0)
        {
            ASSERT(direntp->d_type == DT_DIR, "Expected %s to be %d but got %d", direntp->d_name, DT_DIR, direntp->d_type);
            continue;
        }
        if (strcmp(direntp->d_name, "empty_dir") == 0)
        {
            ASSERT(direntp->d_type == DT_DIR, "Expected %s to be %d but got %d", direntp->d_name, DT_DIR, direntp->d_type);
            continue;
        }

        ASSERT(0, "Unexpected file %s in directory!", direntp->d_name);
    }

    ASSERT(file_count == 5, "ROMFS returned wrong number of files %d to us!", file_count);
    ASSERT(closedir(dirp) == 0, "ROMFS failed to close directory, errno is \"%s\" (%d)!", strerror(errno), errno);

    // Test a subdirectory
    dirp = opendir("rom://subdir/");
    ASSERT(dirp != 0, "Failed to open directory in ROMFS, errno is \"%s\" (%d)!", strerror(errno), errno);

    file_count = 0;
    while (1)
    {
        errno = 0;
        struct dirent* direntp = readdir( dirp );
        if (direntp == NULL)
        {
            ASSERT(errno == 0, "Got error return from readdir, errno is \"%s\" (%d)!", strerror(errno), errno);
            break;
        }
        file_count++;

        if (strcmp(direntp->d_name, ".") == 0)
        {
            ASSERT(direntp->d_type == DT_DIR, "Expected %s to be %d but got %d", direntp->d_name, DT_DIR, direntp->d_type);
            continue;
        }
        if (strcmp(direntp->d_name, "..") == 0)
        {
            ASSERT(direntp->d_type == DT_DIR, "Expected %s to be %d but got %d", direntp->d_name, DT_DIR, direntp->d_type);
            continue;
        }
        if (strcmp(direntp->d_name, "test.txt") == 0)
        {
            ASSERT(direntp->d_type == DT_REG, "Expected %s to be %d but got %d", direntp->d_name, DT_REG, direntp->d_type);
            continue;
        }
        if (strcmp(direntp->d_name, "file.txt") == 0)
        {
            ASSERT(direntp->d_type == DT_REG, "Expected %s to be %d but got %d", direntp->d_name, DT_REG, direntp->d_type);
            continue;
        }

        ASSERT(0, "Unexpected file %s in directory!", direntp->d_name);
    }

    ASSERT(file_count == 4, "ROMFS returned wrong number of files %d to us!", file_count);
    ASSERT(closedir(dirp) == 0, "ROMFS failed to close directory, errno is \"%s\" (%d)!", strerror(errno), errno);

    // Test a subdirectory with no suffix slash.
    dirp = opendir("rom://subdir");
    ASSERT(dirp != 0, "Failed to open directory in ROMFS, errno is \"%s\" (%d)!", strerror(errno), errno);

    file_count = 0;
    while (1)
    {
        errno = 0;
        struct dirent* direntp = readdir( dirp );
        if (direntp == NULL)
        {
            ASSERT(errno == 0, "Got error return from readdir, errno is \"%s\" (%d)!", strerror(errno), errno);
            break;
        }
        file_count++;

        if (strcmp(direntp->d_name, ".") == 0)
        {
            ASSERT(direntp->d_type == DT_DIR, "Expected %s to be %d but got %d", direntp->d_name, DT_DIR, direntp->d_type);
            continue;
        }
        if (strcmp(direntp->d_name, "..") == 0)
        {
            ASSERT(direntp->d_type == DT_DIR, "Expected %s to be %d but got %d", direntp->d_name, DT_DIR, direntp->d_type);
            continue;
        }
        if (strcmp(direntp->d_name, "test.txt") == 0)
        {
            ASSERT(direntp->d_type == DT_REG, "Expected %s to be %d but got %d", direntp->d_name, DT_REG, direntp->d_type);
            continue;
        }
        if (strcmp(direntp->d_name, "file.txt") == 0)
        {
            ASSERT(direntp->d_type == DT_REG, "Expected %s to be %d but got %d", direntp->d_name, DT_REG, direntp->d_type);
            continue;
        }

        ASSERT(0, "Unexpected file %s in directory!", direntp->d_name);
    }

    ASSERT(file_count == 4, "ROMFS returned wrong number of files %d to us!", file_count);
    ASSERT(closedir(dirp) == 0, "ROMFS failed to close directory, errno is \"%s\" (%d)!", strerror(errno), errno);

    // Test an empty directory
    dirp = opendir("rom://empty_dir/");
    ASSERT(dirp != 0, "Failed to open directory in ROMFS, errno is \"%s\" (%d)!", strerror(errno), errno);

    file_count = 0;
    while (1)
    {
        errno = 0;
        struct dirent* direntp = readdir( dirp );
        if (direntp == NULL)
        {
            ASSERT(errno == 0, "Got error return from readdir, errno is \"%s\" (%d)!", strerror(errno), errno);
            break;
        }
        file_count++;

        if (strcmp(direntp->d_name, ".") == 0)
        {
            ASSERT(direntp->d_type == DT_DIR, "Expected %s to be %d but got %d", direntp->d_name, DT_DIR, direntp->d_type);
            continue;
        }
        if (strcmp(direntp->d_name, "..") == 0)
        {
            ASSERT(direntp->d_type == DT_DIR, "Expected %s to be %d but got %d", direntp->d_name, DT_DIR, direntp->d_type);
            continue;
        }

        ASSERT(0, "Unexpected file %s in directory!", direntp->d_name);
    }

    ASSERT(file_count == 2, "ROMFS returned wrong number of files %d to us!", file_count);
    ASSERT(closedir(dirp) == 0, "ROMFS failed to close directory, errno is \"%s\" (%d)!", strerror(errno), errno);
    romfs_free_default();
}

void test_romfs_dup(test_context_t *context)
{
    ASSERT(romfs_init_default() == 0, "ROMFS init failed!");

    int fd = open("rom://test.txt", O_RDONLY);
    char buf[128];

    ASSERT(read(fd, buf, 10) == 10, "ROMFS returned wrong bytes read!");
    ASSERT(lseek(fd, 0, SEEK_CUR) == 10, "ROMFS returned wrong location for file!");

    // Now, duplicate it.
    int fd2 = dup(fd);
    ASSERT(fd2 != fd, "Duplicate file descriptor is the same!");
    ASSERT(lseek(fd, 0, SEEK_CUR) == 10, "ROMFS returned wrong location for file!");
    ASSERT(lseek(fd2, 0, SEEK_CUR) == 10, "ROMFS returned wrong location for file!");

    // Read from one, ensure both move.
    ASSERT(read(fd, buf, 1) == 1, "ROMFS returned wrong bytes read!");
    ASSERT(lseek(fd, 0, SEEK_CUR) == 11, "ROMFS returned wrong location for file!");
    ASSERT(lseek(fd2, 0, SEEK_CUR) == 11, "ROMFS returned wrong location for file!");

    ASSERT(read(fd2, buf, 1) == 1, "ROMFS returned wrong bytes read!");
    ASSERT(lseek(fd, 0, SEEK_CUR) == 12, "ROMFS returned wrong location for file!");
    ASSERT(lseek(fd2, 0, SEEK_CUR) == 12, "ROMFS returned wrong location for file!");

    // Close one, make sure we can access the other still.
    close(fd);

    ASSERT(read(fd2, buf, 1) == 1, "ROMFS returned wrong bytes read!");
    ASSERT(read(fd, buf, 1) == -1, "ROMFS returned unexpected success for closed file!");
    ASSERT(lseek(fd2, 0, SEEK_CUR) == 13, "ROMFS returned wrong location for file!");
    ASSERT(lseek(fd, 0, SEEK_CUR) == -1, "ROMFS returned unexpected success for closed file!");

    // Close the other, make sure neither can be accessed.
    close(fd2);
    ASSERT(read(fd2, buf, 1) == -1, "ROMFS returned unexpected success for closed file!");
    ASSERT(lseek(fd2, 0, SEEK_CUR) == -1, "ROMFS returned unexpected success for closed file!");

    romfs_free_default();
}

void test_romfs_realpath(test_context_t *context)
{
    char *abspath;

    ASSERT(romfs_init_default() == 0, "ROMFS init failed!");

    // Test for already absolute root.
    ASSERT(abspath = realpath("rom://", 0), "realpath() returned failure, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT(strcmp(abspath, "rom://") == 0, "realpath() returned invalid canonical path %s!", abspath);
    free(abspath);

    // Test for as many slashes or current directory markers as we want.
    ASSERT(abspath = realpath("rom:///.///.//", 0), "realpath() returned failure, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT(strcmp(abspath, "rom://") == 0, "realpath() returned invalid canonical path %s!", abspath);
    free(abspath);

    // Test for subdirectories.
    ASSERT(abspath = realpath("rom://subdir", 0), "realpath() returned failure, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT(strcmp(abspath, "rom://subdir/") == 0, "realpath() returned invalid canonical path %s!", abspath);
    free(abspath);

    ASSERT(abspath = realpath("rom://subdir/", 0), "realpath() returned failure, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT(strcmp(abspath, "rom://subdir/") == 0, "realpath() returned invalid canonical path %s!", abspath);
    free(abspath);

    // Test for traversal past root.
    ASSERT(abspath = realpath("rom://../subdir/", 0), "realpath() returned failure, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT(strcmp(abspath, "rom://subdir/") == 0, "realpath() returned invalid canonical path %s!", abspath);
    free(abspath);

    // Test for regular traversal.
    ASSERT(abspath = realpath("rom://subdir/..", 0), "realpath() returned failure, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT(strcmp(abspath, "rom://") == 0, "realpath() returned invalid canonical path %s!", abspath);
    free(abspath);

    ASSERT(abspath = realpath("rom://subdir/../", 0), "realpath() returned failure, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT(strcmp(abspath, "rom://") == 0, "realpath() returned invalid canonical path %s!", abspath);
    free(abspath);

    ASSERT(abspath = realpath("rom://subdir/../subdir", 0), "realpath() returned failure, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT(strcmp(abspath, "rom://subdir/") == 0, "realpath() returned invalid canonical path %s!", abspath);
    free(abspath);

    // Test for basic files.
    ASSERT(abspath = realpath("rom://subdir/../test.txt", 0), "realpath() returned failure, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT(strcmp(abspath, "rom://test.txt") == 0, "realpath() returned invalid canonical path %s!", abspath);
    free(abspath);

    ASSERT(abspath = realpath("rom://subdir/file.txt", 0), "realpath() returned failure, errno is \"%s\" (%d)!", strerror(errno), errno);
    ASSERT(strcmp(abspath, "rom://subdir/file.txt") == 0, "realpath() returned invalid canonical path %s!", abspath);
    free(abspath);

    // Test for nonexistent files.
    ASSERT(realpath("rom://inval.txt", 0) == 0, "realpath() returned unexpected success!");
    ASSERT(errno = ENOENT, "realpath() returned invalid errno %d, expected %d", errno, ENOENT);

    ASSERT(realpath("rom://subdir/inval.txt", 0) == 0, "realpath() returned unexpected success!");
    ASSERT(errno = ENOENT, "realpath() returned invalid errno %d, expected %d", errno, ENOENT);

    ASSERT(realpath("rom://nonexistent/test.txt", 0) == 0, "realpath() returned unexpected success!");
    ASSERT(errno = ENOENT, "realpath() returned invalid errno %d, expected %d", errno, ENOENT);

    // Test for file in non-last location.
    ASSERT(realpath("rom://test.txt/subdir/", 0) == 0, "realpath() returned unexpected success!");
    ASSERT(errno = ENOTDIR, "realpath() returned invalid errno %d, expected %d", errno, ENOTDIR);

    ASSERT(realpath("rom://test.txt/", 0) == 0, "realpath() returned unexpected success!");
    ASSERT(errno = ENOTDIR, "realpath() returned invalid errno %d, expected %d", errno, ENOTDIR);

    romfs_free_default();
}
