CROSS ?= m68k-linux-gnu-
CC = $(CROSS)gcc
AS = $(CROSS)as
AR = $(CROSS)ar

CFLAGS = -m68000 -nostdlib -nostartfiles -ffreestanding \
         -Wall -Wextra -O2

OBJS = syscalls.o string.o stdio.o
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
