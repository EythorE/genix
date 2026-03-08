/*
 * crt0.S — User program startup for Genix
 *
 * On entry, the stack contains:
 *   SP+0:  argc    (uint32_t)
 *   SP+4:  argv[0] (char *)
 *   SP+8:  argv[1] (char *)
 *   ...
 *   SP+N:  NULL    (end of argv)
 *   SP+N+4: NULL   (end of envp)
 */
    .text
    .globl _start
_start:
    /* Pick up argc and argv from the stack */
    move.l  (%sp)+, %d0         /* argc */
    move.l  %sp, %a0            /* argv = pointer to argv[0] on stack */

    /* Calculate envp = argv + (argc+1) * 4 */
    move.l  %d0, %d1
    addq.l  #1, %d1
    lsl.l   #2, %d1
    lea     (%a0,%d1.l), %a1    /* envp */

    /* Call main(argc, argv, envp) */
    move.l  %a1, -(%sp)         /* push envp */
    move.l  %a0, -(%sp)         /* push argv */
    move.l  %d0, -(%sp)         /* push argc */
    jsr     main
    lea     12(%sp), %sp        /* clean up args */

    /* exit(return_value) */
    move.l  %d0, %d1            /* exit code in d1 */
    moveq   #1, %d0             /* SYS_EXIT = 1 */
    trap    #0

    /* Not reached */
1:  bra     1b
