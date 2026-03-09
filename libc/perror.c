/*
 * perror / strerror — error message functions for Genix.
 *
 * Genix syscalls return -errno directly. User programs get negative
 * return values. We store the last error for perror/strerror use.
 */

extern int write(int fd, const void *buf, int count);
extern unsigned int strlen(const char *s);

int errno;

/* Error message table — matches kernel errno values */
static const char *const errtab[] = {
    "Success",                  /*  0 */
    "Operation not permitted",  /*  1 EPERM */
    "No such file or directory",/*  2 ENOENT */
    "No such process",          /*  3 ESRCH */
    "Interrupted system call",  /*  4 EINTR */
    "I/O error",                /*  5 EIO */
    "No such device or address",/*  6 ENXIO */
    "Argument list too long",   /*  7 E2BIG */
    "Exec format error",        /*  8 ENOEXEC */
    "Bad file number",          /*  9 EBADF */
    "No child processes",       /* 10 ECHILD */
    "Try again",                /* 11 EAGAIN */
    "Out of memory",            /* 12 ENOMEM */
    "Permission denied",        /* 13 EACCES */
    "Bad address",              /* 14 EFAULT */
    "Block device required",    /* 15 ENOTBLK */
    "Device or resource busy",  /* 16 EBUSY */
    "File exists",              /* 17 EEXIST */
    "Cross-device link",        /* 18 EXDEV */
    "No such device",           /* 19 ENODEV */
    "Not a directory",          /* 20 ENOTDIR */
    "Is a directory",           /* 21 EISDIR */
    "Invalid argument",         /* 22 EINVAL */
    "File table overflow",      /* 23 ENFILE */
    "Too many open files",      /* 24 EMFILE */
    "Not a typewriter",         /* 25 ENOTTY */
    "Text file busy",           /* 26 ETXTBSY */
    "File too large",           /* 27 EFBIG */
    "No space left on device",  /* 28 ENOSPC */
    "Illegal seek",             /* 29 ESPIPE */
    "Read-only file system",    /* 30 EROFS */
    "Too many links",           /* 31 EMLINK */
    "Broken pipe",              /* 32 EPIPE */
};

#define NERR (sizeof(errtab) / sizeof(errtab[0]))

const char *strerror(int errnum)
{
    if (errnum >= 0 && (unsigned int)errnum < NERR)
        return errtab[errnum];
    return "Unknown error";
}

void perror(const char *s)
{
    int e = errno;
    if (s && *s) {
        write(2, s, strlen(s));
        write(2, ": ", 2);
    }
    const char *msg = strerror(e);
    write(2, msg, strlen(msg));
    write(2, "\n", 1);
}
