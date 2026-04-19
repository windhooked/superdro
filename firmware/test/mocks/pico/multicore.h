#ifndef MOCK_PICO_MULTICORE_H
#define MOCK_PICO_MULTICORE_H

#include <stdint.h>
#include <stdbool.h>

typedef void (*multicore_entry_t)(void);
static inline void multicore_launch_core1(multicore_entry_t entry) { (void)entry; }

// Inter-core FIFO — declared here, stubbed per-test or per-mock
extern bool     multicore_fifo_rvalid(void);
extern uint32_t multicore_fifo_pop_blocking(void);
static inline void multicore_fifo_push_blocking(uint32_t data) { (void)data; }

#endif
