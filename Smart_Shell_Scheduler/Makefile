CC = gcc
CFLAGS = -Wall -Wextra -Iinclude -D_POSIX_C_SOURCE=200809L
LDFLAGS = 

# Directories
SRC_DIR = src
INC_DIR = include
BIN_DIR = bin
OBJ_DIR = obj
TEST_DIR = tests

# Targets
SHELL_EXE = $(BIN_DIR)/simpleShell
SCHED_EXE = $(BIN_DIR)/simpleScheduler
TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c, $(BIN_DIR)/%, $(TEST_SRCS))

# Default parameters for "make run"
NCPU ?= 1
TSLICE ?= 500

.PHONY: all clean run

all: setup $(SHELL_EXE) $(SCHED_EXE) $(TEST_BINS)

setup:
	@mkdir -p $(BIN_DIR) $(OBJ_DIR)

# Compile Shell
$(SHELL_EXE): $(OBJ_DIR)/simpleShell.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Compile Scheduler
$(SCHED_EXE): $(OBJ_DIR)/simpleScheduler.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/%: $(TEST_DIR)/%.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	@./$(SHELL_EXE) $(NCPU) $(TSLICE)

clean:
	@echo "Cleaning up..."
	@rm -rf $(BIN_DIR) $(OBJ_DIR)