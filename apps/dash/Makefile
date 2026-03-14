CROSS ?= m68k-linux-gnu-
CC = $(CROSS)gcc
AS = $(CROSS)as
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
LIBGCC = $(shell $(CC) -m68000 -print-libgcc-file-name)

CFLAGS = -m68000 -msep-data -nostdlib -nostartfiles -ffreestanding \
         -I. -I../../libc/include \
         -include config.h \
         -Wall -Wno-implicit-int -Wno-return-type -Wno-parentheses \
         -Wno-implicit-function-declaration -Wno-unused-but-set-variable \
         -Wno-unused-variable -Wno-missing-braces -Wno-old-style-definition \
         -Wno-maybe-uninitialized -Wno-unused-parameter \
         -DSMALL=1 -DJOBS=0 -DBSD=0 -DSHELL \
         -O2

CRT0 = ../crt0.o
LIBC = ../../libc/libc.a
LDSCRIPT ?= ../user-reloc.ld
MKBIN = ../../tools/mkbin

CSRCS = alias.c arith_yacc.c arith_yylex.c builtins.c cd.c error.c \
        eval.c exec.c expand.c histedit.c init.c input.c jobs.c \
        main.c mail.c memalloc.c miscbltin.c mystring.c nodes.c \
        options.c output.c parser.c redir.c show.c signames.c \
        syntax.c system.c trap.c var.c \
        genix_stubs.c \
        bltin/printf.c bltin/test.c bltin/times.c
OBJS = $(CSRCS:.c=.o)

all: dash

$(CRT0):
	$(MAKE) -C .. crt0.o

$(LIBC):
	$(MAKE) -C ../../libc

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

bltin/%.o: bltin/%.c
	@mkdir -p bltin
	$(CC) $(CFLAGS) -c -o $@ $<

dash.elf: $(OBJS) $(CRT0) $(LIBC)
	$(LD) -n -T $(LDSCRIPT) --emit-relocs -o $@ $(CRT0) $(OBJS) $(LIBC) $(LIBGCC)

dash: dash.elf $(MKBIN)
	$(MKBIN) $< $@

$(MKBIN):
	$(MAKE) -C ../../tools mkbin

clean:
	rm -f $(OBJS) dash.elf dash

.PHONY: all clean
