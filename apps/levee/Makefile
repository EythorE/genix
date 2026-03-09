CROSS ?= m68k-linux-gnu-
CC = $(CROSS)gcc
AS = $(CROSS)as
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
LIBGCC = $(shell $(CC) -m68000 -print-libgcc-file-name)

CFLAGS = -m68000 -nostdlib -nostartfiles -ffreestanding \
         -I../../libc/include -Wall -Wno-implicit-int \
         -Wno-return-type -Wno-parentheses -Wno-implicit-function-declaration \
         -Wno-unused-but-set-variable -Wno-unused-variable \
         -Wno-char-subscripts -Wno-maybe-uninitialized \
         -DANSI=1 -DVT52=0 -DTERMCAP=0 -DZTERM=0 -DSIZE=2048 \
         -O2

CRT0 = ../crt0.o
LIBC = ../../libc/libc.a
LDSCRIPT ?= ../user.ld
MKBIN = ../../tools/mkbin

CSRCS = blockio.c display.c editcor.c exec.c find.c \
        globals.c insert.c main.c misc.c modify.c move.c \
        ucsd.c undo.c unixcall.c wildargs.c
OBJS = $(CSRCS:.c=.o)

all: levee

$(CRT0):
	$(MAKE) -C .. crt0.o

$(LIBC):
	$(MAKE) -C ../../libc

%.o: %.c levee.h extern.h proto.h grep.h
	$(CC) $(CFLAGS) -c -o $@ $<

levee.elf: $(OBJS) $(CRT0) $(LIBC)
	$(LD) -T $(LDSCRIPT) -o $@ $(CRT0) $(OBJS) $(LIBC) $(LIBGCC)

levee: levee.elf $(MKBIN)
	$(MKBIN) $< $@

$(MKBIN):
	$(MAKE) -C ../../tools mkbin

clean:
	rm -f $(OBJS) levee.elf levee

.PHONY: all clean
