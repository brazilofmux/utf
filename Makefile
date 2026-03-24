# Makefile — UTF library: compressed DFA Unicode processing + PUA color ops.
#
# Build: make
# Test:  make test
# Clean: make clean

CC       = gcc
CFLAGS   = -O2 -Wall -Wextra -std=c11 -g
# Ragel -G2 generates intentional fallthroughs and unused entry-point constants.
RAGEL_CFLAGS = $(CFLAGS) -Wno-implicit-fallthrough -Wno-unused-const-variable
INCLUDES = -Iinclude
AR       = ar
ARFLAGS  = rcs

LIB      = libutf.a

# Library sources
LIB_SRCS = src/color_ops.c \
           src/cie97.c \
           src/console_width.c \
           src/grapheme.c \
           src/nfc.c \
           tables/unicode_tables.c \
           tables/unicode_tables_ext.c \
           tables/nfc_tables.c \
           tables/charset_tables.c \
           tables/xterm_palette.c

LIB_OBJS = $(LIB_SRCS:.c=.o)

# Test sources
TEST_SRCS = tests/test_color_ops.c
TEST_OBJS = $(TEST_SRCS:.c=.o)
TEST_BIN  = tests/test_color_ops

all: $(LIB)

$(LIB): $(LIB_OBJS)
	$(AR) $(ARFLAGS) $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

src/color_ops.o: src/color_ops.c
	$(CC) $(RAGEL_CFLAGS) $(INCLUDES) -c $< -o $@

# Ragel: regenerate color_ops.c from .rl source
ragel: src/color_ops.rl
	ragel -G2 -C -o src/color_ops.c src/color_ops.rl

test: $(LIB) $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $(TEST_BIN) $(TEST_OBJS) -L. -lutf -lm
	./$(TEST_BIN)

clean:
	rm -f $(LIB_OBJS) $(TEST_OBJS) $(LIB) $(TEST_BIN)

.PHONY: all ragel test clean
