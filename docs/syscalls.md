# Syscall Interface

## Convention

Genix uses a simple register-based syscall convention:

| Register | Purpose |
|----------|---------|
| `d0` | Syscall number (in), return value (out) |
| `d1` | Argument 1 |
| `d2` | Argument 2 |
| `d3` | Argument 3 |
| `d4` | Argument 4 |

Entry via **`TRAP #0`** (vector 32). Return value in `d0`: non-negative
on success, negative `-errno` on failure.

### Comparison with Fuzix

Fuzix uses `TRAP #14` and puts some arguments in address registers
(a0-a2). Since Genix recompiles all apps from source, the C library
handles the mapping — apps never make raw syscalls. Our convention
is cleaner (all data registers) and avoids the address register
complexity.

## Kernel-Side Dispatch

The TRAP #0 handler (`kernel/crt0.S:_vec_syscall`) saves d1-d7/a0-a6
via `MOVEM.L`, pushes d0-d4 as C arguments, and calls:

```c
int32_t syscall_dispatch(uint32_t num, uint32_t a1, uint32_t a2,
                         uint32_t a3, uint32_t a4);
```

The return value stays in `d0`. After `syscall_dispatch()` returns,
the handler restores all registers and executes `RTE`.

## Libc Stubs (`libc/syscalls.S`)

Each stub must save and restore any callee-saved registers (d2-d7,
a2-a6) it uses for arguments. Example:

```asm
/* int read(int fd, void *buf, int count) */
read:
    movem.l %d2-%d3, -(%sp)    /* save callee-saved */
    move.l  12(%sp), %d1       /* fd   (offset adjusted for push) */
    move.l  16(%sp), %d2       /* buf  */
    move.l  20(%sp), %d3       /* count */
    moveq   #3, %d0            /* SYS_READ */
    trap    #0
    movem.l (%sp)+, %d2-%d3    /* restore */
    rts
```

Stubs that only use d0-d1 (like `close`, `getpid`) don't need
MOVEM saves. Stubs using d2+ must save/restore to preserve the
C calling convention.

## Implemented Syscalls

| Number | Name | Signature | Status |
|--------|------|-----------|--------|
| 1 | `SYS_EXIT` | `void _exit(int code)` | Working |
| 3 | `SYS_READ` | `int read(int fd, void *buf, int count)` | Working |
| 4 | `SYS_WRITE` | `int write(int fd, const void *buf, int count)` | Working |
| 5 | `SYS_OPEN` | `int open(const char *path, int flags)` | Working |
| 6 | `SYS_CLOSE` | `int close(int fd)` | Working |
| 7 | `SYS_WAITPID` | `int waitpid(int pid, int *status)` | Stub (-ECHILD) |
| 10 | `SYS_UNLINK` | `int unlink(const char *path)` | Working |
| 11 | `SYS_EXEC` | `int exec(const char *path, const char **argv)` | Working |
| 12 | `SYS_CHDIR` | `int chdir(const char *path)` | Working |
| 13 | `SYS_TIME` | `int time(void)` | Working (returns ticks) |
| 19 | `SYS_LSEEK` | `int lseek(int fd, int offset, int whence)` | Working |
| 20 | `SYS_GETPID` | `int getpid(void)` | Working |
| 37 | `SYS_KILL` | `int kill(int pid, int sig)` | Not implemented |
| 38 | `SYS_RENAME` | `int rename(const char *old, const char *new)` | Working |
| 39 | `SYS_MKDIR` | `int mkdir(const char *path)` | Working |
| 40 | `SYS_RMDIR` | `int rmdir(const char *path)` | Working |
| 41 | `SYS_DUP` | `int dup(int fd)` | Working |
| 42 | `SYS_PIPE` | `int pipe(int pipefd[2])` | Not implemented |
| 45 | `SYS_BRK` | `void *brk(void *addr)` | Working |
| 48 | `SYS_SIGNAL` | `void *signal(int sig, void *handler)` | Not implemented |
| 54 | `SYS_IOCTL` | `int ioctl(int fd, int cmd, void *arg)` | Not implemented |
| 55 | `SYS_FCNTL` | `int fcntl(int fd, int cmd, ...)` | Not implemented |
| 63 | `SYS_DUP2` | `int dup2(int oldfd, int newfd)` | Working |
| 69 | `SYS_SBRK` | `void *sbrk(int incr)` | Working |
| 79 | `SYS_GETCWD` | `char *getcwd(char *buf, int size)` | Not implemented |
| 106 | `SYS_STAT` | `int stat(const char *path, void *buf)` | Working |
| 108 | `SYS_FSTAT` | `int fstat(int fd, void *buf)` | Working |
| 141 | `SYS_GETDENTS` | `int getdents(int fd, void *buf, int n)` | Not implemented |
| 190 | `SYS_VFORK` | `int vfork(void)` | Stub (-ENOSYS) |

### Syscall numbers

Numbers are chosen to match Linux m68k where possible. Since user
programs use the libc stubs (not raw numbers), the exact values are an
implementation detail.

## Adding New Syscalls

1. Add `SYS_FOO` number to `kernel/kernel.h`
2. Implement `sys_foo()` in the appropriate subsystem `.c` file
3. Add `case SYS_FOO:` to `syscall_dispatch()` in `kernel/proc.c`
4. Add libc stub in `libc/syscalls.S`
5. Add host test if the logic is testable
6. Update this table

## Open File Table

The kernel maintains a system-wide open file table (64 entries) and
per-process file descriptor arrays (16 entries each):

```c
struct ofile {
    struct inode *inode;    // NULL for console devices
    uint32_t offset;
    uint16_t flags;         // O_RDONLY, O_WRONLY, O_RDWR, O_APPEND, ...
    uint8_t  refcount;
};
```

File descriptors 0, 1, 2 (stdin, stdout, stderr) are initialized
to console device entries with no backing inode. Read/write on these
fds delegates directly to `devtab[DEV_CONSOLE]`.
