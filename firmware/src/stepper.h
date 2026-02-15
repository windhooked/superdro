#ifndef STEPPER_H
#define STEPPER_H

#include <stdint.h>
#include <stdbool.h>

// Initialize PIO 1 SM 0 for step pulse generation, and dir/enable GPIO pins.
// Stepper starts DISABLED (enable pin HIGH) for safety.
void stepper_init(void);

// Enable/disable the CL57T driver. Active low: true = enabled (pin LOW).
void stepper_enable(bool enabled);

// Set step direction. true = forward (toward headstock), false = reverse.
// Must be called before pushing steps in the new direction.
// CL57T setup time (~5 µs) is satisfied by the Core 0 loop period (20 µs).
void stepper_set_dir(bool forward);

// Get current direction setting.
bool stepper_get_dir(void);

// Push a single step command to the PIO TX FIFO.
// delay_cycles = inter-step period in PIO clock cycles (use stepper_delay_from_rate()).
// Increments/decrements internal position counter based on current direction.
// Returns false if TX FIFO is full (caller should retry next loop iteration).
bool stepper_push_step(uint32_t delay_cycles);

// Calculate the FIFO delay value for a given step rate (steps/second).
// Returns UINT32_MAX for rate <= 0.
uint32_t stepper_delay_from_rate(float steps_per_sec);

// Stop stepping: drain FIFO, SM blocks at pull. Motor holds position.
void stepper_stop(void);

// Get absolute step position (signed). Increments on forward, decrements on reverse.
// Tracks at FIFO push time — may be up to 4 steps ahead of physical position.
int32_t stepper_get_position(void);

// Reset position counter to zero.
void stepper_zero_position(void);

// Set position counter to a specific value.
void stepper_set_position(int32_t pos);

// Check how many TX FIFO slots are available (0–4).
unsigned int stepper_fifo_free(void);

// Called from Core 0 loop. Reserved for future monitoring (CL57T alarm, stall detect).
void stepper_update(void);

#endif // STEPPER_H
