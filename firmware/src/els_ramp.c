#include "els_ramp.h"
#include "pico/stdlib.h"
#include <stdint.h>
#include <limits.h>

void els_ramp_init(els_ramp_state_t *r, uint32_t min_delay,
                   uint32_t max_delay, uint32_t delta) {
    r->phase         = RAMP_IDLE;
    r->min_delay     = min_delay;
    r->max_delay     = max_delay;
    r->delta         = delta;
    r->current_delay = max_delay;
}

void __not_in_flash_func(els_ramp_engage)(els_ramp_state_t *r) {
    r->current_delay = r->max_delay;
    r->phase         = RAMP_ACCEL;
}

void __not_in_flash_func(els_ramp_disengage)(els_ramp_state_t *r) {
    if (r->phase == RAMP_IDLE) return;
    r->phase = RAMP_DECEL;
}

uint32_t __not_in_flash_func(els_ramp_step)(els_ramp_state_t *r) {
    switch (r->phase) {
    case RAMP_IDLE:
        return UINT32_MAX;

    case RAMP_ACCEL:
        if (r->current_delay > r->min_delay + r->delta) {
            r->current_delay -= r->delta;
        } else {
            r->current_delay = r->min_delay;
            r->phase         = RAMP_CRUISE;
        }
        return r->current_delay;

    case RAMP_CRUISE:
        return r->current_delay;

    case RAMP_DECEL:
        if (r->current_delay < r->max_delay - r->delta) {
            r->current_delay += r->delta;
        } else {
            r->current_delay = r->max_delay;
            r->phase         = RAMP_IDLE;
        }
        return r->current_delay;
    }

    return UINT32_MAX;
}

uint32_t __not_in_flash_func(els_ramp_floor)(const els_ramp_state_t *r,
                                              uint32_t bresenham_delay) {
    if (r->phase == RAMP_IDLE) return bresenham_delay;
    return (bresenham_delay > r->current_delay) ? bresenham_delay : r->current_delay;
}

bool __not_in_flash_func(els_ramp_idle)(const els_ramp_state_t *r) {
    return r->phase == RAMP_IDLE;
}
