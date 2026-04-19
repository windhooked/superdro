// Unit tests for els_engine — Bresenham accumulator and rate predictor.
// Compile: gcc -Wall -std=c11 -I mocks -I ../src -o test_bresenham \
//          test_bresenham.c ../src/els_engine.c ../src/config.c mock_flash.c -lm

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include "els_engine.h"
#include "config.h"

// --- Stepper stubs ---
static int32_t stub_pos[AXIS_COUNT];
static int     stub_pushes[AXIS_COUNT];
static bool    stub_dir[AXIS_COUNT];
static bool    stub_full[AXIS_COUNT];

bool stepper_push(stepper_axis_t axis, uint32_t delay) {
    (void)delay;
    if (stub_full[axis]) return false;
    stub_pos[axis] += stub_dir[axis] ? 1 : -1;
    stub_pushes[axis]++;
    return true;
}
void stepper_set_dir(stepper_axis_t axis, bool fwd) { stub_dir[axis] = fwd; }

// --- Pico stubs (not used in engine, but config.c needs flash) ---
#include "pico/stdlib.h"
#include "hardware/flash.h"

#define TEST(name) static void name(void)
#define RUN(name)  do { printf("  " #name "..."); name(); printf(" OK\n"); } while(0)

static void reset(void) {
    memset(stub_pos,    0, sizeof(stub_pos));
    memset(stub_pushes, 0, sizeof(stub_pushes));
    memset(stub_dir,    1, sizeof(stub_dir));
    memset(stub_full,   0, sizeof(stub_full));
    memset(_mock_flash, 0xFF, sizeof(_mock_flash));
}

// Zero cumulative error at ratio 1:1 over N edges
TEST(test_1to1_no_drift) {
    reset();
    els_axis_state_t ax;
    els_engine_axis_init(&ax, AXIS_Z, 1, 1, INT32_MIN, INT32_MAX, 64);
    els_engine_axis_reset(&ax);

    for (int i = 0; i < 10000; i++) {
        els_engine_axis_advance(&ax, 1, 1000);
    }
    // Flush all
    while (ax.backlog > 0) {
        els_engine_axis_flush(&ax, 625, 4);
    }
    assert(ax.position == 10000);
    assert(ax.accumulator == 0);
}

// Ratio 1:2 — one step every two spindle edges
TEST(test_half_ratio_exact) {
    reset();
    els_axis_state_t ax;
    els_engine_axis_init(&ax, AXIS_Z, 1, 2, INT32_MIN, INT32_MAX, 64);
    els_engine_axis_reset(&ax);

    for (int i = 0; i < 1000; i++)
        els_engine_axis_advance(&ax, 1, 1000);
    while (ax.backlog > 0)
        els_engine_axis_flush(&ax, 625, 4);

    assert(ax.position == 500);
    assert(ax.accumulator == 0);
}

// Ratio 7:3 — GCD reduction applied; accumulator zero after exact multiple
TEST(test_non_unit_ratio_zero_error) {
    reset();
    els_axis_state_t ax;
    // 7 steps per 3 spindle edges: after 3 edges → 7 steps exactly
    els_engine_axis_init(&ax, AXIS_Z, 7, 3, INT32_MIN, INT32_MAX, 64);
    els_engine_axis_reset(&ax);

    // Simulate exactly 3000 spindle edges
    for (int i = 0; i < 3000; i++)
        els_engine_axis_advance(&ax, 1, 1000);
    while (ax.backlog > 0)
        els_engine_axis_flush(&ax, 625, 4);

    assert(ax.position == 7000);
    assert(ax.accumulator == 0);
}

// 1M edges at ratio 127:89 — zero residual error
TEST(test_large_run_no_drift) {
    reset();
    els_axis_state_t ax;
    els_engine_axis_init(&ax, AXIS_Z, 127, 89, INT32_MIN, INT32_MAX, 1<<20);
    els_engine_axis_reset(&ax);

    // Process in batches of 89 (one full denominator period)
    int64_t expected = 0;
    for (int batch = 0; batch < 10000; batch++) {
        for (int i = 0; i < 89; i++)
            els_engine_axis_advance(&ax, 1, 1000);
        expected += 127;
    }
    while (ax.backlog > 0)
        els_engine_axis_flush(&ax, 625, 64);

    assert(ax.position == (int32_t)expected);
    assert(ax.accumulator == 0);
}

// Backlog fault fires when |backlog| exceeds threshold
TEST(test_backlog_fault) {
    reset();
    stub_full[AXIS_Z] = true;  // FIFO full — steps can't drain
    els_axis_state_t ax;
    els_engine_axis_init(&ax, AXIS_Z, 1, 1, INT32_MIN, INT32_MAX, 4);
    els_engine_axis_reset(&ax);

    bool faulted = false;
    for (int i = 0; i < 20; i++) {
        if (!els_engine_axis_advance(&ax, 1, 1000)) {
            faulted = true; break;
        }
    }
    assert(faulted);
}

// GCD reduction: 6/4 becomes 3/2
TEST(test_gcd_reduction) {
    els_axis_state_t ax;
    els_engine_axis_init(&ax, AXIS_Z, 6, 4, INT32_MIN, INT32_MAX, 64);
    assert(ax.ratio_num == 3);
    assert(ax.ratio_den == 2);
}

// Negative spindle delta (reverse) decrements position
TEST(test_reverse_tracking) {
    reset();
    els_axis_state_t ax;
    els_engine_axis_init(&ax, AXIS_Z, 1, 1, INT32_MIN, INT32_MAX, 64);
    els_engine_axis_reset(&ax);

    for (int i = 0; i < 100; i++)
        els_engine_axis_advance(&ax, 1, 1000);
    for (int i = 0; i < 60; i++)
        els_engine_axis_advance(&ax, -1, 1000);

    while (ax.backlog != 0)
        els_engine_axis_flush(&ax, 625, 4);

    assert(ax.position == 40);
    assert(ax.accumulator == 0);
}

int main(void) {
    printf("=== test_bresenham ===\n");
    RUN(test_gcd_reduction);
    RUN(test_1to1_no_drift);
    RUN(test_half_ratio_exact);
    RUN(test_non_unit_ratio_zero_error);
    RUN(test_large_run_no_drift);
    RUN(test_backlog_fault);
    RUN(test_reverse_tracking);
    printf("All passed.\n");
    return 0;
}
