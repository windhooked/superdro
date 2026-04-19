// Shared mock state — single definition linked by all test translation units
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Mock flash (two sectors so CONFIG_FLASH_OFFSET = FLASH_SECTOR_SIZE is non-zero)
#define FLASH_SECTOR_SIZE 4096
uint8_t _mock_flash[2 * FLASH_SECTOR_SIZE];
bool _mock_flash_initialized = false;

void _mock_flash_ensure_init(void) {
    if (!_mock_flash_initialized) {
        memset(_mock_flash, 0xFF, 2 * FLASH_SECTOR_SIZE);
        _mock_flash_initialized = true;
    }
}

// Mock time
uint64_t _mock_time_us = 0;

// Mock PIO FIFO
uint32_t _mock_fifo_data[4][16];
int _mock_fifo_count[4] = {0};
int _mock_fifo_read_pos[4] = {0};

// Mock quadrature decoder state
int32_t s_counts[4] = {0};
uint8_t s_prev_state[4] = {0};

// Mock TX FIFO (for stepper PIO)
uint32_t _mock_tx_fifo_data[4][16];
int _mock_tx_fifo_count[4] = {0};
bool _mock_tx_fifo_full[4] = {false};

// Mock GPIO output state
bool _mock_gpio_state[32] = {false};

// Mock watchdog
int _mock_watchdog_fed = 0;
