/*
 * 32-bit unsigned divide and modulo for 68000
 *
 * These are needed because the 68000 only has 16-bit divide instructions.
 * GCC calls these when dividing 32-bit values.
 */

    .section .text

/*
 * unsigned 32-bit divide: d0 = arg1 / arg2
 * args on stack: 4(sp) = dividend, 8(sp) = divisor
 */
    .globl __udivsi3
    .globl __udivsi3_internal
    .type __udivsi3, @function
__udivsi3:
__udivsi3_internal:
    move.l  4(%sp), %d0         /* dividend */
    move.l  8(%sp), %d1         /* divisor */
    tst.l   %d1
    beq     .Ldiv_zero

    /* Fast path: if both operands fit in 16 bits, use DIVU.W (~140 cycles) */
    move.l  %d0, %d1
    or.l    8(%sp), %d1         /* d1 = dividend | divisor */
    swap    %d1
    tst.w   %d1                 /* high word of (dividend|divisor) == 0? */
    bne     .Ludiv_slow
    move.l  8(%sp), %d1         /* reload divisor */
    divu.w  %d1, %d0            /* d0 = 32-bit dividend / 16-bit divisor */
    and.l   #0xFFFF, %d0        /* keep only low word (quotient) */
    rts

.Ludiv_slow:
    move.l  8(%sp), %d1         /* reload divisor */

    /* Generic shift-and-subtract algorithm */
    movem.l %d2-%d4, -(%sp)

    move.l  %d0, %d2            /* dividend -> d2 */
    move.l  %d1, %d3            /* divisor -> d3 */
    clr.l   %d0                 /* quotient = 0 */

    /* Find highest bit of divisor relative to dividend */
    moveq   #31, %d4            /* bit counter */

    /* Shift divisor left until it's >= dividend */
    move.l  %d3, %d1
.Lshift_up:
    cmp.l   %d2, %d1
    bhi     .Lshift_done
    tst.l   %d1
    bmi     .Lshift_done        /* Don't shift past bit 31 */
    add.l   %d1, %d1            /* shift left */
    dbra    %d4, .Lshift_up

.Lshift_done:
    /* Now shift right and subtract */
    addq.l  #1, %d4
.Ldiv_loop:
    add.l   %d0, %d0            /* quotient <<= 1 */
    cmp.l   %d1, %d2
    bcs     .Lno_sub            /* if dividend < shifted divisor, skip */
    sub.l   %d1, %d2            /* dividend -= shifted divisor */
    addq.l  #1, %d0             /* quotient |= 1 */
.Lno_sub:
    lsr.l   #1, %d1             /* shifted divisor >>= 1 */
    cmp.l   %d3, %d1            /* back to original divisor? */
    bcc     .Ldiv_loop          /* if still >= original, continue */

    movem.l (%sp)+, %d2-%d4
    rts

.Ldiv_zero:
    clr.l   %d0
    rts

/*
 * unsigned 32-bit modulo: d0 = arg1 % arg2
 */
    .globl __umodsi3
    .type __umodsi3, @function
__umodsi3:
    move.l  4(%sp), %d0         /* dividend */
    move.l  8(%sp), %d1         /* divisor */
    tst.l   %d1
    beq     .Lmod_zero

    /* Fast path: if both operands fit in 16 bits, use DIVU.W (~140 cycles) */
    move.l  %d0, %d1
    or.l    8(%sp), %d1         /* d1 = dividend | divisor */
    swap    %d1
    tst.w   %d1                 /* high word of (dividend|divisor) == 0? */
    bne     .Lumod_slow
    move.l  8(%sp), %d1         /* reload divisor */
    divu.w  %d1, %d0            /* d0 = 32-bit dividend / 16-bit divisor */
    clr.w   %d0                 /* clear low word (quotient) */
    swap    %d0                 /* move high word (remainder) to low word */
    rts

.Lumod_slow:
    move.l  8(%sp), %d1         /* reload divisor */

    movem.l %d2-%d4, -(%sp)

    move.l  %d0, %d2            /* dividend -> d2 */
    move.l  %d1, %d3            /* divisor -> d3 */

    moveq   #31, %d4

    move.l  %d3, %d1
.Lmod_shift_up:
    cmp.l   %d2, %d1
    bhi     .Lmod_shift_done
    tst.l   %d1
    bmi     .Lmod_shift_done
    add.l   %d1, %d1
    dbra    %d4, .Lmod_shift_up

.Lmod_shift_done:
    addq.l  #1, %d4
.Lmod_loop:
    cmp.l   %d1, %d2
    bcs     .Lmod_no_sub
    sub.l   %d1, %d2
.Lmod_no_sub:
    lsr.l   #1, %d1
    cmp.l   %d3, %d1
    bcc     .Lmod_loop

    move.l  %d2, %d0            /* remainder */
    movem.l (%sp)+, %d2-%d4
    rts

.Lmod_zero:
    clr.l   %d0
    rts

/*
 * signed 32-bit divide
 */
    .globl __divsi3
    .globl __divsi3_internal
    .type __divsi3, @function
__divsi3:
__divsi3_internal:
    move.l  %d2, -(%sp)        /* save d2 */
    move.l  8(%sp), %d0        /* dividend */
    move.l  12(%sp), %d1       /* divisor */
    clr.l   %d2                /* sign flag */
    tst.l   %d0
    bpl     .Ldiv_pos_a
    neg.l   %d0
    addq.l  #1, %d2
.Ldiv_pos_a:
    tst.l   %d1
    bpl     .Ldiv_pos_b
    neg.l   %d1
    addq.l  #1, %d2
.Ldiv_pos_b:
    move.l  %d1, -(%sp)
    move.l  %d0, -(%sp)
    jsr     __udivsi3
    addq.l  #8, %sp
    btst    #0, %d2
    beq     .Ldiv_done
    neg.l   %d0
.Ldiv_done:
    move.l  (%sp)+, %d2
    rts

/*
 * signed 32-bit modulo
 */
    .globl __modsi3
    .type __modsi3, @function
__modsi3:
    move.l  %d2, -(%sp)
    move.l  8(%sp), %d0        /* dividend */
    move.l  12(%sp), %d1       /* divisor */
    move.l  %d0, %d2           /* save dividend sign */
    tst.l   %d0
    bpl     .Lsmod_pos_a
    neg.l   %d0
.Lsmod_pos_a:
    tst.l   %d1
    bpl     .Lsmod_pos_b
    neg.l   %d1
.Lsmod_pos_b:
    move.l  %d1, -(%sp)
    move.l  %d0, -(%sp)
    jsr     __umodsi3
    addq.l  #8, %sp
    tst.l   %d2
    bpl     .Lsmod_done
    neg.l   %d0
.Lsmod_done:
    move.l  (%sp)+, %d2
    rts
