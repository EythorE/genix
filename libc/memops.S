/*
 * memops.S — Optimized memcpy, memset, memmove for Motorola 68000
 *
 * Uses MOVEM.L bulk transfers (32 bytes per iteration) for large
 * aligned operations.  Byte-at-a-time for small or unaligned cases.
 *
 * C ABI: 4(%sp)=dest, 8(%sp)=src/value, 12(%sp)=count.
 * Returns dest in d0.  Preserves d2-d7/a2-a6.
 */

	.text

/* =====================================================================
 * memmove — overlapping-safe copy
 * Forward if dest <= src (falls into memcpy), backward byte copy otherwise.
 * ===================================================================== */
	.globl	memmove
memmove:
	move.l	4(%sp),%a0		/* dest */
	move.l	8(%sp),%a1		/* src */
	move.l	12(%sp),%d1		/* count */
	move.l	%a0,%d0			/* return dest */
	cmpa.l	%a1,%a0
	bls.s	mcpy_fwd		/* dest <= src: forward copy is safe */
	/* Backward byte copy for overlapping case */
	adda.l	%d1,%a0
	adda.l	%d1,%a1
	subq.l	#1,%d1
	bmi.s	.mm_done
.mm_bwd:
	move.b	-(%a1),-(%a0)
	dbra	%d1,.mm_bwd
	subi.l	#0x10000,%d1		/* handle count > 65535 */
	bpl.s	.mm_bwd
.mm_done:
	rts

/* =====================================================================
 * memcpy — non-overlapping forward copy
 *   < 32 bytes → byte loop
 *   >= 32, both long-aligned → MOVEM.L 8 regs = 32 bytes/iteration
 *   >= 32, unaligned → align dest, recheck src, bulk or byte
 * ===================================================================== */
	.globl	memcpy
memcpy:
	move.l	4(%sp),%a0		/* dest */
	move.l	8(%sp),%a1		/* src */
	move.l	12(%sp),%d1		/* count */
	move.l	%a0,%d0			/* return dest */

mcpy_fwd:
	cmpi.l	#32,%d1
	blt	.mc_bytes

	/* Both pointers long-aligned? */
	move.l	%a0,%d0
	move.l	%a1,%d1			/* temporarily borrow d1 */
	or.l	%d1,%d0			/* d0 = dest | src */
	move.l	12(%sp),%d1		/* restore count */
	andi.l	#3,%d0
	bne.s	.mc_align

.mc_bulk:
	movem.l	%d2-%d7/%a2-%a3,-(%sp)	/* save 8 regs */
.mc_blk:
	cmpi.l	#32,%d1
	blt.s	.mc_blk_done
	movem.l	(%a1)+,%d2-%d7/%a2-%a3	/* load 32 bytes (auto-increment) */
	movem.l	%d2-%d7/%a2-%a3,(%a0)	/* store 32 bytes */
	lea	32(%a0),%a0
	subi.l	#32,%d1
	bra.s	.mc_blk
.mc_blk_done:
	movem.l	(%sp)+,%d2-%d7/%a2-%a3	/* restore */
	move.l	4(%sp),%d0		/* return dest */
	bra.s	.mc_bytes		/* copy any remaining bytes */

	/* Align dest to long boundary by copying leading bytes */
.mc_align:
	move.l	%a0,%d0
	andi.l	#3,%d0
	beq.s	.mc_chksrc
	neg.l	%d0
	addi.l	#4,%d0			/* 4 - (dest & 3) = bytes to align */
.mc_al:
	move.b	(%a1)+,(%a0)+
	subq.l	#1,%d1
	subq.l	#1,%d0
	bne.s	.mc_al
.mc_chksrc:
	move.l	%a1,%d0
	andi.l	#3,%d0
	beq.s	.mc_bulk		/* both aligned: use MOVEM.L */
	move.l	4(%sp),%d0		/* src still unaligned: byte copy */
	/* fall through */

.mc_bytes:
	subq.l	#1,%d1
	bmi.s	.mc_done
.mc_bloop:
	move.b	(%a1)+,(%a0)+
	dbra	%d1,.mc_bloop
	subi.l	#0x10000,%d1		/* handle count > 65535 */
	bpl.s	.mc_bloop
.mc_done:
	rts

/* =====================================================================
 * memset — fill memory with byte value
 *   < 32 bytes → byte loop
 *   >= 32, dest long-aligned → replicate byte to 8 regs, MOVEM.L store
 *
 * Register plan:
 *   a0 = current dest pointer
 *   a2 = saved original dest (for return value), saved/restored at entry/exit
 *   d0 = fill value (replicated to 0xVVVVVVVV for bulk)
 *   d1 = remaining byte count
 * Bulk path uses d0/d2-d7/a3 = 8 regs = 32 bytes per MOVEM.L store.
 * ===================================================================== */
	.globl	memset
memset:
	move.l	%a2,-(%sp)		/* save a2 — stack offsets shift +4 */
	move.l	8(%sp),%a0		/* dest */
	move.l	12(%sp),%d0		/* value (low byte used) */
	move.l	16(%sp),%d1		/* count */
	move.l	%a0,%a2			/* a2 = original dest for return */

	cmpi.l	#32,%d1
	blt	.ms_bytes

	/* Replicate low byte across all 4 bytes: 0xVV → 0xVVVVVVVV */
	andi.l	#0xFF,%d0
	move.l	%d0,%d1			/* stash copy */
	lsl.l	#8,%d0
	or.l	%d1,%d0			/* 0x0000VVVV */
	move.l	%d0,%d1
	swap	%d0
	or.l	%d1,%d0			/* 0xVVVVVVVV */

	/* Align dest to long boundary */
	move.l	%a0,%a1
	move.l	%a1,%d1			/* d1 = dest address low bits */
	andi.l	#3,%d1
	beq.s	.ms_aligned
	neg.l	%d1
	addi.l	#4,%d1			/* bytes to next long boundary */
	/* d1 = alignment bytes to write; adjust remaining count */
	move.l	16(%sp),%a1		/* total count in a1 */
	sub.l	%d1,%a1			/* a1 = count after alignment */
.ms_al:
	move.b	%d0,(%a0)+
	subq.l	#1,%d1
	bne.s	.ms_al
	move.l	%a1,%d1			/* d1 = remaining count */
	bra.s	.ms_bulk

.ms_aligned:
	move.l	16(%sp),%d1		/* full count */

.ms_bulk:
	/* Save d2-d7/a3, fill them with the replicated pattern */
	movem.l	%d2-%d7/%a3,-(%sp)
	move.l	%d0,%d2
	move.l	%d0,%d3
	move.l	%d0,%d4
	move.l	%d0,%d5
	move.l	%d0,%d6
	move.l	%d0,%d7
	move.l	%d0,%a3			/* d0/d2-d7/a3 = 8 regs = 32 bytes */

.ms_blk:
	cmpi.l	#32,%d1
	blt.s	.ms_blk_done
	movem.l	%d0/%d2-%d7/%a3,(%a0)	/* store 32 bytes */
	lea	32(%a0),%a0
	subi.l	#32,%d1
	bra.s	.ms_blk

.ms_blk_done:
	movem.l	(%sp)+,%d2-%d7/%a3	/* restore */
	/* d0 is 0xVVVVVVVV; low byte is correct for move.b tail */

.ms_bytes:
	subq.l	#1,%d1
	bmi.s	.ms_done
.ms_bloop:
	move.b	%d0,(%a0)+
	dbra	%d1,.ms_bloop
	subi.l	#0x10000,%d1		/* handle count > 65535 */
	bpl.s	.ms_bloop
.ms_done:
	move.l	%a2,%d0			/* return original dest */
	move.l	(%sp)+,%a2		/* restore a2 */
	rts
