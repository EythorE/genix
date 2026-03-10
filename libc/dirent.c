/*
 * opendir/readdir/closedir — wraps SYS_GETDENTS
 *
 * The kernel's SYS_GETDENTS returns raw dirent_disk structs (32 bytes:
 * 2-byte inode + 30-byte name). We read one entry at a time and return
 * a pointer to a struct dirent in the DIR.
 */
#include <dirent.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

/* Syscall: int getdents(int fd, void *buf, int count) */
extern int getdents(int fd, void *buf, int count);

DIR *opendir(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;

    DIR *d = (DIR *)malloc(sizeof(DIR));
    if (!d) {
        close(fd);
        return NULL;
    }
    d->fd = fd;
    d->pos = 0;
    return d;
}

struct dirent *readdir(DIR *dirp)
{
    if (!dirp)
        return NULL;

    /* Read one raw directory entry (32 bytes: 2-byte inum + 30-byte name) */
    for (;;) {
        int n = getdents(dirp->fd, &dirp->ent, 32);
        if (n <= 0)
            return NULL;

        /* Skip empty slots (inode == 0) */
        if (dirp->ent.d_ino != 0)
            return &dirp->ent;
    }
}

int closedir(DIR *dirp)
{
    if (!dirp)
        return -1;
    int rc = close(dirp->fd);
    free(dirp);
    return rc;
}
