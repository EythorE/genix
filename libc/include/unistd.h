/* Genix unistd.h */
#ifndef _UNISTD_H
#define _UNISTD_H

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Seek modes */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* Standard file descriptors */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* Existing syscall stubs */
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

/* UID/GID stubs (single-user system — always return 0) */
int   getuid(void);
int   geteuid(void);
int   getgid(void);
int   getegid(void);
int   setuid(int uid);
int   setgid(int gid);
int   seteuid(int uid);
int   setegid(int gid);

/* Process group stubs */
int   getpgrp(void);
int   setpgid(int pid, int pgid);
int   getppid(void);
int   tcsetpgrp(int fd, int pgrp);
int   tcgetpgrp(int fd);

/* Misc stubs */
unsigned int alarm(unsigned int seconds);
unsigned int sleep(unsigned int seconds);
long  sysconf(int name);
int   access(const char *path, int mode);

/* access() mode flags */
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

/* sysconf names */
#define _SC_CLK_TCK 2

#endif
