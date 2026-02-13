#ifndef MOCK_HARDWARE_FLASH_H
#define MOCK_HARDWARE_FLASH_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define FLASH_PAGE_SIZE 256
#define FLASH_SECTOR_SIZE 4096
#define PICO_FLASH_SIZE_BYTES FLASH_SECTOR_SIZE

// Shared mock flash storage (defined in mock_flash.c)
// XIP_BASE + CONFIG_FLASH_OFFSET must equal &_mock_flash[0].
// Since PICO_FLASH_SIZE_BYTES = FLASH_SECTOR_SIZE, CONFIG_FLASH_OFFSET = 0.
extern uint8_t _mock_flash[FLASH_SECTOR_SIZE];
extern bool _mock_flash_initialized;
extern void _mock_flash_ensure_init(void);

#define XIP_BASE ((uintptr_t)(_mock_flash_ensure_init(), _mock_flash))

static inline void flash_range_erase(uint32_t offset, uint32_t count) {
    (void)offset; (void)count;
    _mock_flash_ensure_init();
    memset(_mock_flash, 0xFF, sizeof(_mock_flash));
}

static inline void flash_range_program(uint32_t offset, const uint8_t *data, uint32_t count) {
    (void)offset;
    _mock_flash_ensure_init();
    if (count <= sizeof(_mock_flash)) {
        memcpy(_mock_flash, data, count);
    }
}

#endif
