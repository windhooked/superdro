#ifndef MOCK_PICO_CYW43_ARCH_H
#define MOCK_PICO_CYW43_ARCH_H

#include <stdbool.h>

#define CYW43_WL_GPIO_LED_PIN 0

static inline int cyw43_arch_init(void) { return 0; }
static inline void cyw43_arch_gpio_put(int pin, bool value) { (void)pin; (void)value; }

#endif
