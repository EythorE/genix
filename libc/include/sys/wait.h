/* Genix sys/wait.h — process status macros */
#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

/* waitpid() options */
#define WNOHANG    1
#define WUNTRACED  2

/*
 * Status encoding (matches kernel do_waitpid):
 *   Normal exit:  bits 15-8 = exit status, bits 7-0 = 0
 *   Signal death: bits 15-8 = 0, bits 7-0 = signal number
 *   Stopped:      bits 15-8 = signal, bits 7-0 = 0x7F
 */
#define WIFEXITED(s)    (((s) & 0x7F) == 0)
#define WEXITSTATUS(s)  (((s) >> 8) & 0xFF)
#define WIFSIGNALED(s)  (((s) & 0x7F) != 0 && ((s) & 0x7F) != 0x7F)
#define WTERMSIG(s)     ((s) & 0x7F)
#define WIFSTOPPED(s)   (((s) & 0xFF) == 0x7F)
#define WSTOPSIG(s)     (((s) >> 8) & 0xFF)

#endif
