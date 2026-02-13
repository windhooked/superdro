#ifndef MOCK_HARDWARE_WATCHDOG_H
#define MOCK_HARDWARE_WATCHDOG_H

#include <stdint.h>
#include <stdbool.h>

extern int _mock_watchdog_fed;

static inline void watchdog_enable(uint32_t timeout_ms, bool pause_on_debug) {
    (void)timeout_ms; (void)pause_on_debug;
}
static inline void watchdog_update(void) {
    _mock_watchdog_fed++;
}

#endif
