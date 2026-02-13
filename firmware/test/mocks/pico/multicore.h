#ifndef MOCK_PICO_MULTICORE_H
#define MOCK_PICO_MULTICORE_H

typedef void (*multicore_entry_t)(void);
static inline void multicore_launch_core1(multicore_entry_t entry) { (void)entry; }

#endif
