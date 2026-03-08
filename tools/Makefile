CC = gcc
CFLAGS = -Wall -Wextra -O2

all: mkfs.minifs mkbin

mkfs.minifs: mkfs.c
	$(CC) $(CFLAGS) -o $@ $<

mkbin: mkbin.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f mkfs.minifs mkbin

.PHONY: all clean
