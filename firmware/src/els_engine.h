#ifndef ELS_ENGINE_H
#define ELS_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    AXIS_Z = 0,
    AXIS_X = 1,
    AXIS_C = 2,
    AXIS_COUNT = 3,
} stepper_axis_t;

// Per-axis Bresenham + rate-predictor state.
typedef struct {
    int64_t  ratio_num;           // GCD-reduced (always > 0)
    int64_t  ratio_den;           // GCD-reduced (always > 0)
    int64_t  accumulator;         // signed; source of truth for position
    int32_t  position;            // steps emitted since last reset (signed)
    int32_t  soft_min_steps;      // FAULT if position goes below
    int32_t  soft_max_steps;      // FAULT if position goes above
    int32_t  backlog;             // steps due but not yet pushed (signed)
    int32_t  backlog_threshold;   // |backlog| > threshold → FAULT
    uint32_t predicted_delay;     // PIO cycles per step (updated on overflow)
    stepper_axis_t axis;
} els_axis_state_t;

// Init one axis state. ratio_num/den must be GCD-reduced and > 0.
// soft_min/max from config (INT32_MIN/MAX = disabled).
// Returns false if ratio would exceed the axis max step rate at max spindle RPM.
// Not hot-path; not ISR-safe.
bool els_engine_axis_init(els_axis_state_t *ax, stepper_axis_t axis,
                          int64_t ratio_num, int64_t ratio_den,
                          int32_t soft_min, int32_t soft_max,
                          int32_t backlog_threshold);

// Reset accumulator, position, and backlog to zero (call at engage origin snap).
// Core 0. __not_in_flash_func.
void els_engine_axis_reset(els_axis_state_t *ax);

// Advance accumulator by spindle_delta × ratio_num.
// Counts overflows into backlog (signed: + = forward steps, - = reverse).
// Updates predicted_delay using rate_eps (spindle edges-per-second EMA).
// Returns false if |backlog| > backlog_threshold after advance.
// Core 0. __not_in_flash_func.
bool els_engine_axis_advance(els_axis_state_t *ax, int32_t spindle_delta,
                             uint32_t rate_eps);

// Push up to max_steps from backlog into the stepper FIFO using effective_delay.
// Returns steps actually pushed (may be < max_steps if FIFO is full).
// Returns -1 if a soft limit would be crossed (FAULT; no steps emitted beyond limit).
// Core 0. __not_in_flash_func.
int32_t els_engine_axis_flush(els_axis_state_t *ax, uint32_t effective_delay,
                              int32_t max_steps);

// True if |backlog| > backlog_threshold.
// Core 0. __not_in_flash_func.
bool els_engine_axis_backlog_fault(const els_axis_state_t *ax);

// GCD for ratio reduction. Pure integer math; usable from any context.
int64_t els_gcd(int64_t a, int64_t b);

#endif // ELS_ENGINE_H
