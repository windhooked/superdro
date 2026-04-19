// Unit tests for els_ramp — trapezoidal velocity profile.
// Compile: gcc -Wall -std=c11 -I mocks -I ../src -o test_ramp \
//          test_ramp.c ../src/els_ramp.c -lm

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include "els_ramp.h"

#define TEST(name) static void name(void)
#define RUN(name)  do { printf("  " #name "..."); name(); printf(" OK\n"); } while(0)

#define MIN_DELAY  625u    // 200 kHz at 125 MHz
#define MAX_DELAY  125000u // 1 step/ms
#define DELTA      500u

TEST(test_idle_returns_max) {
    els_ramp_state_t r;
    els_ramp_init(&r, MIN_DELAY, MAX_DELAY, DELTA);
    assert(els_ramp_idle(&r));
    assert(els_ramp_step(&r) == UINT32_MAX);
}

TEST(test_engage_starts_accel) {
    els_ramp_state_t r;
    els_ramp_init(&r, MIN_DELAY, MAX_DELAY, DELTA);
    els_ramp_engage(&r);
    assert(r.phase == RAMP_ACCEL);
    assert(r.current_delay == MAX_DELAY);
}

TEST(test_accel_decrements_delay) {
    els_ramp_state_t r;
    els_ramp_init(&r, MIN_DELAY, MAX_DELAY, DELTA);
    els_ramp_engage(&r);

    uint32_t prev = UINT32_MAX;
    for (int i = 0; i < 300; i++) {
        uint32_t d = els_ramp_step(&r);
        if (r.phase == RAMP_CRUISE) {
            assert(d == MIN_DELAY);
            break;
        }
        assert(d < prev);
        prev = d;
    }
    assert(r.phase == RAMP_CRUISE);
}

TEST(test_cruise_returns_min_delay) {
    els_ramp_state_t r;
    els_ramp_init(&r, MIN_DELAY, MAX_DELAY, DELTA);
    els_ramp_engage(&r);

    while (r.phase == RAMP_ACCEL) els_ramp_step(&r);
    assert(r.phase == RAMP_CRUISE);

    for (int i = 0; i < 10; i++) {
        assert(els_ramp_step(&r) == MIN_DELAY);
    }
}

TEST(test_decel_increments_to_idle) {
    els_ramp_state_t r;
    els_ramp_init(&r, MIN_DELAY, MAX_DELAY, DELTA);
    els_ramp_engage(&r);
    while (r.phase != RAMP_CRUISE) els_ramp_step(&r);

    els_ramp_disengage(&r);
    assert(r.phase == RAMP_DECEL);

    uint32_t prev = MIN_DELAY;
    while (!els_ramp_idle(&r)) {
        uint32_t d = els_ramp_step(&r);
        if (r.phase != RAMP_IDLE) {
            assert(d >= prev);
            prev = d;
        }
    }
    assert(els_ramp_idle(&r));
}

TEST(test_floor_returns_larger) {
    els_ramp_state_t r;
    els_ramp_init(&r, MIN_DELAY, MAX_DELAY, DELTA);
    els_ramp_engage(&r);
    // In ACCEL, current_delay starts at MAX_DELAY
    uint32_t floor_val = els_ramp_floor(&r, MIN_DELAY); // Bresenham fast, ramp slow
    assert(floor_val == r.current_delay);

    while (r.phase != RAMP_CRUISE) els_ramp_step(&r);
    // At cruise, current_delay == MIN_DELAY (625). Bresenham delay 500 is FASTER
    // than ramp allows → floor returns the ramp limit (625), not 500.
    floor_val = els_ramp_floor(&r, 500u);
    assert(floor_val == r.current_delay);  // == 625 == MIN_DELAY

    // Bresenham delay 1000 > cruise delay 625 → Bresenham is slower → use 1000.
    floor_val = els_ramp_floor(&r, 1000u);
    assert(floor_val == 1000u);
}

TEST(test_disengage_from_accel_reaches_idle) {
    els_ramp_state_t r;
    els_ramp_init(&r, MIN_DELAY, MAX_DELAY, DELTA);
    els_ramp_engage(&r);
    // Take a few accel steps then disengage early
    for (int i = 0; i < 5; i++) els_ramp_step(&r);
    els_ramp_disengage(&r);
    int iters = 0;
    while (!els_ramp_idle(&r) && iters < 10000) {
        els_ramp_step(&r);
        iters++;
    }
    assert(els_ramp_idle(&r));
}

int main(void) {
    printf("=== test_ramp ===\n");
    RUN(test_idle_returns_max);
    RUN(test_engage_starts_accel);
    RUN(test_accel_decrements_delay);
    RUN(test_cruise_returns_min_delay);
    RUN(test_decel_increments_to_idle);
    RUN(test_floor_returns_larger);
    RUN(test_disengage_from_accel_reaches_idle);
    printf("All passed.\n");
    return 0;
}
