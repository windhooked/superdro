CC ?= gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -g -I mocks -I ../src
LDFLAGS = -lm

SRC_DIR = ../src
MOCK_SRCS = mock_flash.c
TESTS = test_config test_protocol test_encoder

.PHONY: all clean test

all: $(TESTS)

test: $(TESTS)
	@echo "=== Running firmware unit tests ==="
	@for t in $(TESTS); do echo "--- $$t ---" && ./$$t || exit 1; done
	@echo "=== All firmware tests passed ==="

test_config: test_config.c $(SRC_DIR)/config.c $(MOCK_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_protocol: test_protocol.c $(SRC_DIR)/protocol.c $(SRC_DIR)/config.c $(SRC_DIR)/encoder.c $(MOCK_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_encoder: test_encoder.c $(SRC_DIR)/encoder.c $(SRC_DIR)/config.c $(MOCK_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TESTS)
