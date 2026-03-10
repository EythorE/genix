CROSS ?= m68k-linux-gnu-
CC = $(CROSS)gcc
AS = $(CROSS)as
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
LIBGCC = $(shell $(CC) -m68000 -print-libgcc-file-name)

CFLAGS = -m68000 -nostdlib -nostartfiles -ffreestanding \
         -I../libc/include -Wall -Wextra -O2

CRT0 = crt0.o
LIBC = ../libc/libc.a
LDSCRIPT = user.ld
MKBIN = ../tools/mkbin

# Programs to build
PROGRAMS = hello echo cat wc head true false \
           tail tee yes basename dirname rev nl cmp cut tr uniq imshow

# Cancel make's built-in rule that compiles .c directly to executable
%: %.c

all: $(PROGRAMS)

# Build crt0
crt0.o: crt0.S
	$(CC) $(CFLAGS) -c -o $@ $<

# Build libc
$(LIBC):
	$(MAKE) -C ../libc

# Pattern rule: .c -> .o
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Pattern rule: .o -> .elf (link with crt0 + libc)
%.elf: %.o $(CRT0) $(LIBC) $(LDSCRIPT)
	$(LD) -n -T $(LDSCRIPT) -o $@ $(CRT0) $< $(LIBC) $(LIBGCC)

# Pattern rule: .elf -> genix binary (via mkbin)
%: %.elf $(MKBIN)
	$(MKBIN) $< $@

$(MKBIN):
	$(MAKE) -C ../tools mkbin

clean:
	rm -f *.o *.elf $(PROGRAMS)

.PHONY: all clean
