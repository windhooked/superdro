// Unit tests for soft-limit enforcement in els_engine_axis_flush.
// Compile: gcc -Wall -std=c11 -I mocks -I ../src -o test_softlimit \
//          test_softlimit.c ../src/els_engine.c ../src/config.c mock_flash.c -lm

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include "els_engine.h"
#include "config.h"
#include "hardware/flash.h"
#include "pico/stdlib.h"

static int32_t stub_pos_track = 0;
static bool    stub_dir_fwd   = true;

bool stepper_push(stepper_axis_t axis, uint32_t d) {
    (void)axis; (void)d;
    stub_pos_track += stub_dir_fwd ? 1 : -1;
    return true;
}
void stepper_set_dir(stepper_axis_t axis, bool fwd) {
    (void)axis;
    stub_dir_fwd = fwd;
}

#define TEST(name) static void name(void)
#define RUN(name)  do { printf("  " #name "..."); name(); printf(" OK\n"); } while(0)

// No steps emitted beyond the max soft limit
TEST(test_max_limit_stops_flush) {
    stub_pos_track = 0;
    stub_dir_fwd   = true;

    els_axis_state_t ax;
    els_engine_axis_init(&ax, AXIS_Z, 1, 1, INT32_MIN, 10, 1000);
    els_engine_axis_reset(&ax);

    // Advance enough to queue 15 forward steps
    for (int i = 0; i < 15; i++)
        els_engine_axis_advance(&ax, 1, 1000);

    assert(ax.backlog == 15);

    // First flush: should push 10 steps then return -1 on the 11th
    int32_t pushed = els_engine_axis_flush(&ax, 625, 15);
    assert(pushed == -1);
    // Position should be at most 10 (limit)
    assert(ax.position <= 10);
}

// No steps emitted beyond the min soft limit (reverse)
TEST(test_min_limit_stops_flush) {
    stub_pos_track = 0;
    stub_dir_fwd   = false;

    els_axis_state_t ax;
    els_engine_axis_init(&ax, AXIS_Z, 1, 1, -10, INT32_MAX, 1000);
    els_engine_axis_reset(&ax);

    // Advance in reverse
    for (int i = 0; i < 15; i++)
        els_engine_axis_advance(&ax, -1, 1000);

    int32_t pushed = els_engine_axis_flush(&ax, 625, 15);
    assert(pushed == -1);
    assert(ax.position >= -10);
}

// Disabled limits (INT32_MIN/MAX) never trigger
TEST(test_disabled_limits_never_fault) {
    stub_pos_track = 0;
    stub_dir_fwd   = true;

    els_axis_state_t ax;
    els_engine_axis_init(&ax, AXIS_Z, 1, 1, INT32_MIN, INT32_MAX, 1000000);
    els_engine_axis_reset(&ax);

    for (int i = 0; i < 500; i++)
        els_engine_axis_advance(&ax, 1, 1000);

    int32_t pushed = els_engine_axis_flush(&ax, 625, 500);
    assert(pushed == 500);
    assert(ax.position == 500);
}

// Position exactly at limit: one more step → FAULT
TEST(test_limit_boundary_exact) {
    stub_pos_track = 0;
    stub_dir_fwd   = true;

    els_axis_state_t ax;
    els_engine_axis_init(&ax, AXIS_Z, 1, 1, INT32_MIN, 5, 1000);
    els_engine_axis_reset(&ax);

    for (int i = 0; i < 5; i++)
        els_engine_axis_advance(&ax, 1, 1000);
    int32_t pushed = els_engine_axis_flush(&ax, 625, 5);
    assert(pushed == 5);
    assert(ax.position == 5);

    // One more step beyond limit
    els_engine_axis_advance(&ax, 1, 1000);
    int32_t r = els_engine_axis_flush(&ax, 625, 1);
    assert(r == -1);
}

int main(void) {
    printf("=== test_softlimit ===\n");
    RUN(test_max_limit_stops_flush);
    RUN(test_min_limit_stops_flush);
    RUN(test_disabled_limits_never_fault);
    RUN(test_limit_boundary_exact);
    printf("All passed.\n");
    return 0;
}
