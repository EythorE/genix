# Tests Makefile
CC = gcc
CFLAGS = -Wall -Wextra -g -O0 -Wno-builtin-declaration-mismatch

TESTS = test_string test_mem

all: $(TESTS)

test_string: test_string.c testutil.h ../kernel/string.c
	$(CC) $(CFLAGS) -o $@ test_string.c

test_mem: test_mem.c testutil.h ../kernel/mem.c
	$(CC) $(CFLAGS) -o $@ test_mem.c

check: $(TESTS)
	@fail=0; \
	for t in $(TESTS); do \
		echo "=== $$t ==="; \
		./$$t || fail=1; \
	done; \
	exit $$fail

clean:
	rm -f $(TESTS)

.PHONY: all check clean
