/* Genix minimal dirent.h */
#ifndef _DIRENT_H
#define _DIRENT_H

#include <stdint.h>

#define NAME_MAX 30

/* On-disk directory entry (matches kernel's dirent_disk) */
struct dirent {
    uint16_t d_ino;
    char     d_name[NAME_MAX];
};

typedef struct {
    int      fd;       /* open file descriptor for the directory */
    int      pos;      /* bytes read so far (for internal tracking) */
    struct dirent ent; /* current entry buffer */
} DIR;

DIR           *opendir(const char *path);
struct dirent *readdir(DIR *dirp);
int            closedir(DIR *dirp);

#endif
