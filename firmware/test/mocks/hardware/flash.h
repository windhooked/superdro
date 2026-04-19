#ifndef MOCK_HARDWARE_FLASH_H
#define MOCK_HARDWARE_FLASH_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define FLASH_PAGE_SIZE 256
#define FLASH_SECTOR_SIZE 4096
// Two sectors so CONFIG_FLASH_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE = FLASH_SECTOR_SIZE (non-zero).
#define PICO_FLASH_SIZE_BYTES (2 * FLASH_SECTOR_SIZE)

// Shared mock flash storage (defined in mock_flash.c).
// Two sectors: CONFIG_FLASH_OFFSET lands in the second sector.
extern uint8_t _mock_flash[2 * FLASH_SECTOR_SIZE];
extern bool _mock_flash_initialized;
extern void _mock_flash_ensure_init(void);

#define XIP_BASE ((uintptr_t)(_mock_flash_ensure_init(), _mock_flash))

static inline void flash_range_erase(uint32_t offset, uint32_t count) {
    _mock_flash_ensure_init();
    if (offset + count <= sizeof(_mock_flash))
        memset(_mock_flash + offset, 0xFF, count);
}

static inline void flash_range_program(uint32_t offset, const uint8_t *data, uint32_t count) {
    _mock_flash_ensure_init();
    if (offset + count <= sizeof(_mock_flash))
        memcpy(_mock_flash + offset, data, count);
}

#endif
