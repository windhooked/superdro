#include "els_engine.h"
#include "stepper.h"
#include "pico/stdlib.h"
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#define SYS_HZ 125000000u

int64_t els_gcd(int64_t a, int64_t b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) {
        int64_t t = b;
        b = a % b;
        a = t;
    }
    return a;
}

bool els_engine_axis_init(els_axis_state_t *ax, stepper_axis_t axis,
                          int64_t ratio_num, int64_t ratio_den,
                          int32_t soft_min, int32_t soft_max,
                          int32_t backlog_threshold) {
    if (ratio_num <= 0 || ratio_den <= 0) return false;

    int64_t g = els_gcd(ratio_num, ratio_den);
    ax->ratio_num = ratio_num / g;
    ax->ratio_den = ratio_den / g;
    ax->axis = axis;
    ax->soft_min_steps = soft_min;
    ax->soft_max_steps = soft_max;
    ax->backlog_threshold = backlog_threshold;
    ax->accumulator = 0;
    ax->position = 0;
    ax->backlog = 0;
    ax->predicted_delay = UINT32_MAX;
    return true;
}

void __not_in_flash_func(els_engine_axis_reset)(els_axis_state_t *ax) {
    ax->accumulator = 0;
    ax->position    = 0;
    ax->backlog     = 0;
    ax->predicted_delay = UINT32_MAX;
}

bool __not_in_flash_func(els_engine_axis_advance)(els_axis_state_t *ax,
                                                   int32_t spindle_delta,
                                                   uint32_t rate_eps) {
    if (spindle_delta == 0) return true;

    ax->accumulator += (int64_t)spindle_delta * ax->ratio_num;

    // Count forward overflows
    while (ax->accumulator >= ax->ratio_den) {
        ax->accumulator -= ax->ratio_den;
        ax->backlog++;
    }
    // Count reverse overflows
    while (ax->accumulator <= -ax->ratio_den) {
        ax->accumulator += ax->ratio_den;
        ax->backlog--;
    }

    // Update predicted delay on any backlog change
    if (ax->backlog != 0 && rate_eps > 0) {
        // step_delay = SYS_HZ × ratio_den / (rate_eps × ratio_num)
        uint64_t num = (uint64_t)SYS_HZ * (uint64_t)ax->ratio_den;
        uint64_t den = (uint64_t)rate_eps * (uint64_t)ax->ratio_num;
        ax->predicted_delay = (den > 0) ? (uint32_t)(num / den) : UINT32_MAX;
    }

    return !els_engine_axis_backlog_fault(ax);
}

int32_t __not_in_flash_func(els_engine_axis_flush)(els_axis_state_t *ax,
                                                    uint32_t effective_delay,
                                                    int32_t max_steps) {
    int32_t pushed = 0;

    while (pushed < max_steps && ax->backlog != 0) {
        bool forward = (ax->backlog > 0);
        int32_t next = ax->position + (forward ? 1 : -1);

        // Soft limit check before emitting
        if (next > ax->soft_max_steps || next < ax->soft_min_steps) {
            return -1;  // FAULT: would cross limit
        }

        if (!stepper_push(ax->axis, effective_delay)) {
            break;  // FIFO full; caller retries next iteration
        }
        stepper_set_dir(ax->axis, forward);

        ax->position = next;
        ax->backlog += forward ? -1 : +1;
        pushed++;
    }

    return pushed;
}

bool __not_in_flash_func(els_engine_axis_backlog_fault)(const els_axis_state_t *ax) {
    int32_t b = ax->backlog;
    if (b < 0) b = -b;
    return b > ax->backlog_threshold;
}
