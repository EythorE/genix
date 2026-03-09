CROSS ?= m68k-linux-gnu-
CC = $(CROSS)gcc
AS = $(CROSS)as
AR = $(CROSS)ar

CFLAGS = -m68000 -nostdlib -nostartfiles -ffreestanding \
         -Wall -Wextra -O2 -Iinclude

# divmod.o provides __udivsi3/__umodsi3/__divsi3/__modsi3 with pure
# 68000 instructions. The system libgcc may contain 68020-only BSR.L
# in these routines, causing illegal instruction faults on real 68000.
# By including divmod.o in libc.a, our safe versions take priority.
OBJS = syscalls.o string.o stdio.o divmod.o
TARGET = libc.a

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) rcs $@ $^

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
