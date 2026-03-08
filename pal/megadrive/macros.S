/*
 * Mega Drive assembly macros
 *
 * Adapted from FUZIX platform-megadrive
 * Source: https://github.com/EythorE/FUZIX/tree/megadrive/Kernel/platform/platform-megadrive
 */

#==============================================================
# Z80 MACROS
#==============================================================
# We can't access Z80 RAM while it's running, but we can request
# its bus (which will pause the Z80). Note that depending what
# the Z80 is doing, it may take a bit until the bus is free.
#==============================================================

.equ Z80Ram, 0xA00000
.equ Z80BusReq, 0xA11100
.equ Z80Reset, 0xA11200

    .macro PauseZ80
        move.w  #0x100, (Z80BusReq)
    .endm
    .macro WaitZ80
        btst    #0, (Z80BusReq)
        bne.s   wait_z80
    .endm
    .macro FastPauseZ80
        move.w  #0x100, (Z80BusReq)
    .endm
    .macro ResumeZ80
        move.w  #0, (Z80BusReq)
    .endm
