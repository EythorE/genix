CC = gcc
CFLAGS = -Wall -Wextra -O2

all: mkfs.minifs

mkfs.minifs: mkfs.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f mkfs.minifs

.PHONY: all clean
