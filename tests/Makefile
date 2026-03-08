# Tests Makefile
CC = gcc
CFLAGS = -Wall -Wextra -g -O0 -Wno-builtin-declaration-mismatch -I../pal

TESTS = test_string test_mem test_exec

all: $(TESTS)

test_string: test_string.c testutil.h ../kernel/string.c
	$(CC) $(CFLAGS) -o $@ test_string.c

test_mem: test_mem.c testutil.h ../kernel/mem.c
	$(CC) $(CFLAGS) -o $@ test_mem.c

test_exec: test_exec.c testutil.h
	$(CC) $(CFLAGS) -o $@ test_exec.c

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
