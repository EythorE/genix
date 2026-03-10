/* Genix minimal unistd.h */
#ifndef _UNISTD_H
#define _UNISTD_H

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Seek modes */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

void  _exit(int code) __attribute__((noreturn));
int   read(int fd, void *buf, int count);
int   write(int fd, const void *buf, int count);
int   open(const char *path, int flags, ...);
int   close(int fd);
int   lseek(int fd, int offset, int whence);
int   dup(int fd);
int   dup2(int oldfd, int newfd);
int   getpid(void);
int   chdir(const char *path);
int   mkdir(const char *path);
int   unlink(const char *path);
int   rename(const char *oldp, const char *newp);
void *sbrk(int incr);
void *brk(void *addr);
int   isatty(int fd);
int   rmdir(const char *path);
char *getcwd(char *buf, int size);
int   pipe(int pipefd[2]);
int   vfork(void);
int   waitpid(int pid, int *status, int options);
int   execve(const char *path, char *const argv[], char *const envp[]);

#endif
