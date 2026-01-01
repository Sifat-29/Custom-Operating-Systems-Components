.PHONY: all clean

CC := gcc
# -nostdlib: Do not link libc
# -no-pie: Disable position independent executable (easier for simple loaders)
# -fno-builtin: Prevent compiler from generating built-in calls
CFLAGS := -m32 -no-pie -nostdlib -fno-builtin

# Default to compiling all .c files in the directory
SRCS := $(wildcard *.c)
OBJS := $(SRCS:.c=)

all: $(OBJS)

%: %.c
	@$(CC) $(CFLAGS) $< -o $@

clean:
	@rm -f $(OBJS)