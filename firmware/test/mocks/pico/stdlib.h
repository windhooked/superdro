#ifndef MOCK_PICO_STDLIB_H
#define MOCK_PICO_STDLIB_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

// Pico SDK uses 'uint' throughout
typedef unsigned int uint;

// Flash placement attribute — no-op on host
#ifndef __not_in_flash_func
#define __not_in_flash_func(name) name
#endif

// Time (must be declared before use)
extern uint64_t _mock_time_us;

// time_us_32 wraps the 64-bit mock time
static inline uint32_t time_us_32(void) { return (uint32_t)_mock_time_us; }

// Stubs for Pico SDK functions used in firmware

static inline void stdio_init_all(void) {}
static inline void sleep_ms(uint32_t ms) { (void)ms; }
static inline void sleep_us(uint64_t us) { (void)us; }
static inline void gpio_init(uint gpio) { (void)gpio; }
static inline void gpio_set_dir(uint gpio, bool out) { (void)gpio; (void)out; }
static inline void gpio_pull_up(uint gpio) { (void)gpio; }
static inline bool gpio_get(uint gpio) { (void)gpio; return true; /* not pressed */ }
static inline void gpio_set_function(uint gpio, uint fn) { (void)gpio; (void)fn; }

// GPIO output state tracking (defined in mock_flash.c)
extern bool _mock_gpio_state[32];
static inline void gpio_put(uint gpio, bool value) {
    if (gpio < 32) _mock_gpio_state[gpio] = value;
}

#define GPIO_IN  false
#define GPIO_OUT true
#define GPIO_IRQ_EDGE_FALL 0x04
#define GPIO_FUNC_PIO0 6
#define GPIO_FUNC_PIO1 7

typedef void (*gpio_irq_callback_t)(uint gpio, uint32_t events);
static inline void gpio_set_irq_enabled_with_callback(uint gpio, uint32_t events,
    bool enabled, gpio_irq_callback_t cb) {
    (void)gpio; (void)events; (void)enabled; (void)cb;
}

// Time helpers
static inline uint64_t time_us_64(void) { return _mock_time_us; }
static inline void mock_set_time_us(uint64_t t) { _mock_time_us = t; }

typedef uint64_t absolute_time_t;
static inline absolute_time_t get_absolute_time(void) { return _mock_time_us; }
static inline uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000); }

// Error codes
#define PICO_ERROR_TIMEOUT (-1)
static inline int getchar_timeout_us(uint32_t us) { (void)us; return PICO_ERROR_TIMEOUT; }

#endif // MOCK_PICO_STDLIB_H
