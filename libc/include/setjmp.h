/* Genix setjmp.h — nonlocal goto */
#ifndef _SETJMP_H
#define _SETJMP_H

/*
 * jmp_buf holds d2-d7, a2-a6 (11 regs × 4 bytes = 44 bytes)
 * plus SP at offset 44, total 48 bytes = 12 longs.
 */
typedef long jmp_buf[12];

int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

/* POSIX aliases — Genix has no signal mask to save/restore */
typedef long sigjmp_buf[12];
#define sigsetjmp(env, savemask)  setjmp(env)
#define siglongjmp(env, val)     longjmp(env, val)

/* BSD aliases used by some programs */
#define _setjmp(env)       setjmp(env)
#define _longjmp(env, val) longjmp(env, val)

#endif
