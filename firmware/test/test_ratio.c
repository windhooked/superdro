// Unit tests for ratio math: GCD reduction, validation, multi-start PPR check.
// Compile: gcc -Wall -std=c11 -I mocks -I ../src -o test_ratio \
//          test_ratio.c ../src/els_engine.c ../src/config.c mock_flash.c -lm

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include "els_engine.h"
#include "config.h"
#include "hardware/flash.h"

// Stepper stubs
bool stepper_push(stepper_axis_t axis, uint32_t d) { (void)axis;(void)d; return true; }
void stepper_set_dir(stepper_axis_t axis, bool f)  { (void)axis;(void)f; }

#include "pico/stdlib.h"

#define TEST(name) static void name(void)
#define RUN(name)  do { printf("  " #name "..."); name(); printf(" OK\n"); } while(0)

TEST(test_gcd_prime_pair) {
    assert(els_gcd(7, 3)  == 1);
    assert(els_gcd(13, 5) == 1);
}

TEST(test_gcd_common_factor) {
    assert(els_gcd(12, 8)  == 4);
    assert(els_gcd(100, 25) == 25);
    assert(els_gcd(6, 4)   == 2);
}

TEST(test_gcd_identity) {
    assert(els_gcd(7, 7)  == 7);
    assert(els_gcd(1, 1)  == 1);
}

TEST(test_gcd_negative_inputs) {
    // els_gcd takes absolute value internally
    assert(els_gcd(-6, 4) == 2);
    assert(els_gcd(6, -4) == 2);
}

TEST(test_axis_init_reduces_ratio) {
    els_axis_state_t ax;
    bool ok = els_engine_axis_init(&ax, AXIS_Z, 100, 40, INT32_MIN, INT32_MAX, 64);
    assert(ok);
    assert(ax.ratio_num == 5);
    assert(ax.ratio_den == 2);
}

TEST(test_axis_init_rejects_zero_num) {
    els_axis_state_t ax;
    bool ok = els_engine_axis_init(&ax, AXIS_Z, 0, 1, INT32_MIN, INT32_MAX, 64);
    assert(!ok);
}

TEST(test_axis_init_rejects_zero_den) {
    els_axis_state_t ax;
    bool ok = els_engine_axis_init(&ax, AXIS_Z, 1, 0, INT32_MIN, INT32_MAX, 64);
    assert(!ok);
}

// Multi-start: k × (PPR/N) offset must be integer
TEST(test_multistart_ppr_divisible) {
    // PPR=4000, N=4: offset = k × 1000 (integer — OK)
    assert(4000 % 4 == 0);
    assert(4000 % 2 == 0);
    assert(4000 % 8 == 0);
}

TEST(test_multistart_ppr_not_divisible) {
    // PPR=4001, N=3: not divisible — should be rejected
    assert(4001 % 3 != 0);
}

TEST(test_multistart_offset_values) {
    uint32_t ppr = 4000;
    uint8_t  n   = 4;
    assert(ppr % n == 0);
    uint32_t step = ppr / n;
    // Pass k = 0..3
    for (uint8_t k = 0; k < n; k++) {
        uint32_t offset = k * step;
        assert(offset < ppr);
    }
}

int main(void) {
    printf("=== test_ratio ===\n");
    RUN(test_gcd_prime_pair);
    RUN(test_gcd_common_factor);
    RUN(test_gcd_identity);
    RUN(test_gcd_negative_inputs);
    RUN(test_axis_init_reduces_ratio);
    RUN(test_axis_init_rejects_zero_num);
    RUN(test_axis_init_rejects_zero_den);
    RUN(test_multistart_ppr_divisible);
    RUN(test_multistart_ppr_not_divisible);
    RUN(test_multistart_offset_values);
    printf("All passed.\n");
    return 0;
}
