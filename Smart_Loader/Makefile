.PHONY: all loader launcher test run clean

# Default Parameters
TEST_FILE := linear_access
POLICY := FIFO
PAGES := 2

all: loader launcher test

loader:
	@echo "--- Building Loader ---"
	@$(MAKE) --no-print-directory -C loader

launcher: loader
	@echo "--- Building Launcher ---"
	@$(MAKE) --no-print-directory -C launcher

test:
	@echo "--- Building Tests ---"
	@$(MAKE) --no-print-directory -C test

# Usage: make run TEST_FILE=random_access POLICY=RANDOM PAGES=5
run: all
	@echo "--- Running $(TEST_FILE) with $(POLICY) and $(PAGES) max pages ---"
	@./bin/launch ./test/$(TEST_FILE) $(POLICY) $(PAGES)

clean:
	$(MAKE) --no-print-directory -C test clean
	$(MAKE) --no-print-directory -C launcher clean
	$(MAKE) --no-print-directory -C loader clean
	rm -rf bin