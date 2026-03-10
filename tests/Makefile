# Tests Makefile
CC = gcc
CFLAGS = -Wall -Wextra -g -O0 -Wno-builtin-declaration-mismatch -I../pal

TESTS = test_string test_mem test_exec test_proc test_libc

all: $(TESTS)

test_string: test_string.c testutil.h ../kernel/string.c
	$(CC) $(CFLAGS) -o $@ test_string.c

test_mem: test_mem.c testutil.h ../kernel/mem.c
	$(CC) $(CFLAGS) -o $@ test_mem.c

test_exec: test_exec.c testutil.h
	$(CC) $(CFLAGS) -o $@ test_exec.c

test_proc: test_proc.c testutil.h
	$(CC) $(CFLAGS) -o $@ test_proc.c

test_libc: test_libc.c testutil.h ../libc/strtol.c ../libc/getopt.c ../libc/perror.c ../libc/sprintf.c ../libc/isatty.c
	$(CC) $(CFLAGS) -I../libc/include -o $@ test_libc.c

check: $(TESTS)
	@fail=0; \
	for t in $(TESTS); do \
		echo "=== $$t ==="; \
		./$$t || fail=1; \
	done; \
	exit $$fail

clean:
	rm -f $(TESTS) *.o

.PHONY: all check clean
