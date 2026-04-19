#ifndef SPINDLE_H
#define SPINDLE_H

#include <stdint.h>
#include <stdbool.h>

// Initialize spindle PIO (PIO0 SM0, 3-pin quadrature+index), DMA ring,
// and rate estimator. Call after pio programs loaded, before els_fsm_init.
void spindle_init(void);

// Drain DMA ring. Updates count, direction, rate EMA, and index latch.
// Returns net signed quadrature delta since last call (+ = forward, - = reverse).
// Core 0 hot path. __not_in_flash_func.
int32_t spindle_update(void);

// Cumulative quadrature count since boot (signed, wraps at int32 boundary).
// Core 0 only. __not_in_flash_func.
int32_t spindle_read_count(void);

// Spindle rate as edges-per-second EMA (unsigned). 0 if stopped.
// Core 0 only. __not_in_flash_func.
uint32_t spindle_read_rate_eps(void);

// Current direction: +1 forward, -1 reverse, 0 stopped.
// Core 0 only. __not_in_flash_func.
int8_t spindle_direction(void);

// True if index latch fired and has not been cleared.
// Core 0 only. __not_in_flash_func.
bool spindle_index_latched(void);

// Consume the index latch. Call once after THREADING_ARMED transitions out.
// Core 0 only. __not_in_flash_func.
void spindle_index_latch_clear(void);

// True if last inter-index interval deviated >10% from EMA (glitch or missing pulse).
// Core 0 only. __not_in_flash_func.
bool spindle_index_fault(void);

// Arm thread-start synchronization. Call before entering THREADING_ARMED.
//   pulses == 0: latch fires on next index rising edge (single-start).
//   pulses  > 0: latch fires after `pulses` quadrature edges post-index (multi-start).
// Caller must validate PPR % N == 0 before calling.
// Core 0 only. __not_in_flash_func.
void spindle_arm_start_offset(uint32_t pulses);

// RPM computed from rate EMA (for display / rate-validation use only;
// not used in the Bresenham hot path). May be called from Core 1.
float spindle_read_rpm(void);

#endif // SPINDLE_H
