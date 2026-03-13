CC = gcc
CFLAGS = -Wall -Wextra -O2

all: mkfs.minifs mkbin romfix

mkfs.minifs: mkfs.c
	$(CC) $(CFLAGS) -o $@ $<

mkbin: mkbin.c
	$(CC) $(CFLAGS) -o $@ $<

romfix: romfix.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f mkfs.minifs mkbin romfix

.PHONY: all clean
