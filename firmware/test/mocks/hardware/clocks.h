#ifndef MOCK_HARDWARE_CLOCKS_H
#define MOCK_HARDWARE_CLOCKS_H

#include <stdint.h>

typedef enum {
    clk_sys = 0,
} clock_handle_t;

static inline uint32_t clock_get_hz(clock_handle_t clock) {
    (void)clock;
    return 133000000; // 133 MHz (RP2040 default)
}

#endif // MOCK_HARDWARE_CLOCKS_H
