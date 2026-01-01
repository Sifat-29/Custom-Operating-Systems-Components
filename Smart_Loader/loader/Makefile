.PHONY: all clean

CC := gcc
CFLAGS := -m32 -fPIC -shared
OUTPUT_DIR := ../bin
TARGET_LIB := $(OUTPUT_DIR)/libsmartloader.so

# Add swap_manager.c to the source list
SRCS := smartLoader.c swap_manager.c 
HEADERS := loader.h replacement_algos.h swap_manager.h

all: $(TARGET_LIB)

$(TARGET_LIB): $(SRCS) $(HEADERS)
	@mkdir -p $(OUTPUT_DIR)
	@$(CC) $(CFLAGS) $(SRCS) -o $(TARGET_LIB)

clean:
	@rm -f $(TARGET_LIB)