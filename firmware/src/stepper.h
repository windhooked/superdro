#ifndef STEPPER_H
#define STEPPER_H

#include <stdint.h>
#include <stdbool.h>
#include "els_engine.h"  // for stepper_axis_t

// Initialize all three stepper axes (PIO1 SM0=Z, SM1=X, SM2=C).
// Loads the stepper PIO program once; configures per-axis pulse widths.
// Call before els_fsm_init. Not ISR-safe.
void stepper_init(void);

// Enable or disable an axis driver. Z/X: active-low enable pin. C: no enable pin.
// Core 0. __not_in_flash_func.
void stepper_enable(stepper_axis_t axis, bool en);

// Set step direction for an axis. true = forward.
// Core 0. __not_in_flash_func.
void stepper_set_dir(stepper_axis_t axis, bool forward);

// Push one step-delay word to the axis TX FIFO (non-blocking).
// Returns false if FIFO is full.
// Core 0. __not_in_flash_func.
bool stepper_push(stepper_axis_t axis, uint32_t delay_cycles);

// Free TX FIFO slots for the given axis (0–4).
// Core 0. __not_in_flash_func.
unsigned int stepper_fifo_free(stepper_axis_t axis);

// Stop axis: drain FIFO, restart SM. Motor holds torque.
// Core 0. __not_in_flash_func.
void stepper_stop(stepper_axis_t axis);

// Compute delay_cycles for a target step rate (steps/sec).
// Returns UINT32_MAX if rate is 0 or exceeds hardware maximum.
// Pure math; callable from any context.
uint32_t stepper_delay_for_rate(stepper_axis_t axis, uint32_t steps_per_sec);

// -- Legacy single-axis API (Z only) for backward compat with main.c --
void     stepper_enable_z(bool en);
void     stepper_set_dir_z(bool forward);
bool     stepper_get_dir_z(void);
bool     stepper_push_step(uint32_t delay_cycles);   // Z only
uint32_t stepper_delay_from_rate(float steps_per_sec);
void     stepper_stop_z(void);
int32_t  stepper_get_position(void);                 // Z position
void     stepper_zero_position(void);
void     stepper_set_position(int32_t pos);
unsigned int stepper_fifo_free_z(void);
void     stepper_update(void);

#endif // STEPPER_H
