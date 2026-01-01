.PHONY: all clean

CC     := gcc
CFLAGS := -m32 -I../loader
# Pointing to ../bin for libraries and linking against libsmartloader.so
LDFLAGS:= -L../bin -lsmartloader
# Setting RPATH so the binary finds the .so at runtime relative to itself
RPATH  := -Wl,-rpath,'$$ORIGIN'

OUTPUT_DIR := ../bin
TARGET := $(OUTPUT_DIR)/launch

all: $(TARGET)

$(TARGET): launch.c
	@mkdir -p $(OUTPUT_DIR)
	@$(CC) $(CFLAGS) $< $(LDFLAGS) $(RPATH) -o $@

clean:
	@rm -f $(TARGET)