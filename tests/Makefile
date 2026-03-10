# Tests Makefile
CC = gcc
CFLAGS = -Wall -Wextra -g -O0 -Wno-builtin-declaration-mismatch -I../pal

TESTS = test_string test_mem test_exec test_proc test_libc test_vdp test_signal test_redir test_tty test_fs test_buf test_kprintf test_pipe

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

test_vdp: test_vdp.c testutil.h ../kernel/dev_vdp.h
	$(CC) $(CFLAGS) -o $@ test_vdp.c

test_signal: test_signal.c testutil.h
	$(CC) $(CFLAGS) -o $@ test_signal.c

test_redir: test_redir.c testutil.h
	$(CC) $(CFLAGS) -o $@ test_redir.c

test_tty: test_tty.c testutil.h
	$(CC) $(CFLAGS) -o $@ test_tty.c

test_fs: test_fs.c testutil.h ../kernel/fs.c ../kernel/kernel.h
	$(CC) $(CFLAGS) -o $@ test_fs.c

test_buf: test_buf.c testutil.h ../kernel/buf.c
	$(CC) $(CFLAGS) -o $@ test_buf.c

test_kprintf: test_kprintf.c testutil.h ../kernel/kprintf.c
	$(CC) $(CFLAGS) -o $@ test_kprintf.c

test_pipe: test_pipe.c testutil.h
	$(CC) $(CFLAGS) -o $@ test_pipe.c

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
