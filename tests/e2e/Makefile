CC ?= gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -g \
    -I ../../firmware/test/mocks \
    -I ../../firmware/src
LDFLAGS = -lm

FW_SRC = ../../firmware/src
MOCK_SRC = ../../firmware/test

.PHONY: test clean

firmware_sim: firmware_sim.c $(FW_SRC)/config.c $(FW_SRC)/protocol.c $(FW_SRC)/encoder.c $(MOCK_SRC)/mock_flash.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test: firmware_sim
	python3 test_e2e.py

clean:
	rm -f firmware_sim
