#ifndef ELS_RAMP_H
#define ELS_RAMP_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    RAMP_IDLE   = 0,
    RAMP_ACCEL  = 1,
    RAMP_CRUISE = 2,
    RAMP_DECEL  = 3,
} ramp_phase_t;

typedef struct {
    ramp_phase_t phase;
    uint32_t current_delay;   // PIO cycles between steps (large = slow)
    uint32_t min_delay;       // PIO cycles at peak speed
    uint32_t max_delay;       // PIO cycles at start/stop
    uint32_t delta;           // delay change per step (linear ramp)
} els_ramp_state_t;

// Init ramp from config values. min_delay derived from max_step_rate.
// Not hot-path.
void els_ramp_init(els_ramp_state_t *r, uint32_t min_delay,
                   uint32_t max_delay, uint32_t delta);

// Begin acceleration (IDLE → ACCEL).
// Core 0. __not_in_flash_func.
void els_ramp_engage(els_ramp_state_t *r);

// Begin deceleration (CRUISE/ACCEL → DECEL → IDLE).
// Core 0. __not_in_flash_func.
void els_ramp_disengage(els_ramp_state_t *r);

// Advance ramp by one step. Returns effective delay for this step.
// Returns UINT32_MAX when phase == RAMP_IDLE (no step should emit).
// Core 0. __not_in_flash_func.
uint32_t els_ramp_step(els_ramp_state_t *r);

// Floor for blending ramp with Bresenham:
//   effective_delay = max(bresenham_delay, ramp->current_delay)
// Returns 0 when ramp is CRUISE and bresenham_delay is faster (ramp transparent).
// Core 0. __not_in_flash_func.
uint32_t els_ramp_floor(const els_ramp_state_t *r, uint32_t bresenham_delay);

// True if ramp has fully stopped (phase == RAMP_IDLE).
// Core 0. __not_in_flash_func.
bool els_ramp_idle(const els_ramp_state_t *r);

#endif // ELS_RAMP_H
