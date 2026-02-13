#ifndef SAFETY_H
#define SAFETY_H

#include <stdbool.h>

// Initialize safety systems: E-stop input, watchdog, LED
void safety_init(void);

// Check E-stop state (true = E-stop active / triggered)
bool safety_estop_active(void);

// Feed watchdog (call from Core 0 loop)
void safety_watchdog_feed(void);

// Update LED blink pattern (call from Core 1 loop)
void safety_led_update(void);

// Check if alarm state is active
bool safety_alarm_active(void);

// Clear alarm (only if E-stop released)
bool safety_alarm_clear(void);

// Read debounced button states
bool button_engage_pressed(void);
bool button_feed_hold_pressed(void);
bool button_cycle_start_pressed(void);

// Call periodically to debounce buttons (~1 kHz)
void safety_debounce_update(void);

#endif // SAFETY_H
