MUSASHI_DIR = musashi

# Musashi sources
MUSASHI_SRCS = $(MUSASHI_DIR)/m68kcpu.c $(MUSASHI_DIR)/m68kdasm.c \
               $(MUSASHI_DIR)/softfloat/softfloat.c
MUSASHI_GEN  = $(MUSASHI_DIR)/m68kops.c
MUSASHI_GENH = $(MUSASHI_DIR)/m68kops.h
MUSASHI_MAKE = $(MUSASHI_DIR)/m68kmake

CC = gcc
CFLAGS = -Wall -Wextra -O2 -I. -I$(MUSASHI_DIR)
LDFLAGS = -lm

TARGET = emu68k

SRCS = emu68k.c $(MUSASHI_SRCS) $(MUSASHI_GEN)
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(MUSASHI_GENH) $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

# Musashi code generator: generates m68kops.c and m68kops.h
$(MUSASHI_MAKE): $(MUSASHI_DIR)/m68kmake.c
	$(CC) -o $@ $<

$(MUSASHI_GEN) $(MUSASHI_GENH): $(MUSASHI_MAKE) $(MUSASHI_DIR)/m68k_in.c
	cd $(MUSASHI_DIR) && ./m68kmake

# Override musashi's conf with ours
$(MUSASHI_DIR)/m68kcpu.o: $(MUSASHI_DIR)/m68kcpu.c m68kconf.h
	$(CC) $(CFLAGS) -include m68kconf.h -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) $(MUSASHI_MAKE) $(MUSASHI_GEN) $(MUSASHI_GENH)

.PHONY: all clean
