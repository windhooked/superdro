CC ?= gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -g -I mocks -I ../src
LDFLAGS = -lm

SRC_DIR = ../src
MOCK_SRCS = mock_flash.c
MOCK_SPINDLE = mock_spindle.c
ELS_SRCS = $(SRC_DIR)/els_engine.c $(SRC_DIR)/els_ramp.c
FSM_SRCS = $(SRC_DIR)/els_fsm.c $(ELS_SRCS)
TESTS = test_config test_protocol test_encoder test_stepper test_els \
        test_bresenham test_ramp test_ratio test_softlimit test_fsm test_multistart

.PHONY: all clean test

all: $(TESTS)

test: $(TESTS)
	@echo "=== Running firmware unit tests ==="
	@for t in $(TESTS); do echo "--- $$t ---" && ./$$t || exit 1; done
	@echo "=== All firmware tests passed ==="

test_config: test_config.c $(SRC_DIR)/config.c $(MOCK_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

FSM_SRCS = $(SRC_DIR)/els_fsm.c $(ELS_SRCS)

test_protocol: test_protocol.c $(SRC_DIR)/protocol.c $(SRC_DIR)/els.c $(SRC_DIR)/stepper.c $(SRC_DIR)/config.c $(FSM_SRCS) $(MOCK_SRCS) $(MOCK_SPINDLE) mock_encoder.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_encoder: test_encoder.c $(SRC_DIR)/encoder.c $(SRC_DIR)/config.c $(MOCK_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_stepper: test_stepper.c $(SRC_DIR)/stepper.c $(SRC_DIR)/config.c $(MOCK_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_els: test_els.c $(SRC_DIR)/els.c $(SRC_DIR)/stepper.c $(SRC_DIR)/config.c $(FSM_SRCS) $(MOCK_SRCS) $(MOCK_SPINDLE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_bresenham: test_bresenham.c $(ELS_SRCS) $(SRC_DIR)/config.c $(MOCK_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_ramp: test_ramp.c $(SRC_DIR)/els_ramp.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_ratio: test_ratio.c $(ELS_SRCS) $(SRC_DIR)/config.c $(MOCK_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_softlimit: test_softlimit.c $(ELS_SRCS) $(SRC_DIR)/config.c $(MOCK_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_fsm: test_fsm.c $(SRC_DIR)/els_fsm.c $(ELS_SRCS) $(SRC_DIR)/config.c $(MOCK_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_multistart: test_multistart.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TESTS)
