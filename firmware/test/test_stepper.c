// Unit tests for stepper.c — Z stepper PIO driver
// Compile with: gcc -I mocks -I ../src -o test_stepper test_stepper.c ../src/stepper.c ../src/config.c mock_flash.c -lm

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include "stepper.h"
#include "config.h"
#include "pins.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "stepper.pio.h"

#define TEST(name) static void name(void)
#define RUN(name) do { printf("  " #name "..."); name(); printf(" OK\n"); } while(0)

static void reset_mocks(void) {
    _mock_time_us = 0;
    for (int i = 0; i < 4; i++) {
        mock_tx_fifo_clear(i);
        mock_fifo_clear(i);
    }
    memset(_mock_gpio_state, 0, sizeof(_mock_gpio_state));
}

TEST(test_init_starts_disabled) {
    reset_mocks();
    config_load();
    stepper_init();
    // Enable pin (GP10) should be HIGH = disabled
    assert(_mock_gpio_state[PIN_Z_ENABLE] == true);
}

TEST(test_enable_disable) {
    reset_mocks();
    config_load();
    stepper_init();

    // Enable: active low → pin should go LOW
    stepper_enable(true);
    assert(_mock_gpio_state[PIN_Z_ENABLE] == false);

    // Disable: pin should go HIGH
    stepper_enable(false);
    assert(_mock_gpio_state[PIN_Z_ENABLE] == true);
}

TEST(test_direction_forward) {
    reset_mocks();
    config_load();
    stepper_init();

    stepper_set_dir(true);
    assert(_mock_gpio_state[PIN_Z_DIR] == true);
    assert(stepper_get_dir() == true);
}

TEST(test_direction_reverse) {
    reset_mocks();
    config_load();
    stepper_init();

    stepper_set_dir(false);
    assert(_mock_gpio_state[PIN_Z_DIR] == false);
    assert(stepper_get_dir() == false);
}

TEST(test_push_step_forward) {
    reset_mocks();
    config_load();
    stepper_init();

    stepper_set_dir(true);
    for (int i = 0; i < 10; i++) {
        bool ok = stepper_push_step(1000);
        assert(ok);
    }
    assert(stepper_get_position() == 10);
}

TEST(test_push_step_reverse) {
    reset_mocks();
    config_load();
    stepper_init();

    stepper_set_dir(false);
    for (int i = 0; i < 5; i++) {
        bool ok = stepper_push_step(1000);
        assert(ok);
    }
    assert(stepper_get_position() == -5);
}

TEST(test_direction_change) {
    reset_mocks();
    config_load();
    stepper_init();

    stepper_set_dir(true);
    for (int i = 0; i < 5; i++) {
        stepper_push_step(1000);
    }

    stepper_set_dir(false);
    for (int i = 0; i < 3; i++) {
        stepper_push_step(1000);
    }

    assert(stepper_get_position() == 2);
}

TEST(test_fifo_full_rejects) {
    reset_mocks();
    config_load();
    stepper_init();

    // Mark FIFO as full
    _mock_tx_fifo_full[0] = true;

    bool ok = stepper_push_step(1000);
    assert(!ok);
    // Position should NOT change when FIFO is full
    assert(stepper_get_position() == 0);
}

TEST(test_stop_clears) {
    reset_mocks();
    config_load();
    stepper_init();

    stepper_push_step(1000);
    stepper_push_step(2000);
    assert(_mock_tx_fifo_count[0] == 2);

    stepper_stop();
    // After stop, TX FIFO should be cleared
    assert(_mock_tx_fifo_count[0] == 0);
}

TEST(test_zero_position) {
    reset_mocks();
    config_load();
    stepper_init();

    stepper_set_dir(true);
    for (int i = 0; i < 7; i++) {
        stepper_push_step(1000);
    }
    assert(stepper_get_position() == 7);

    stepper_zero_position();
    assert(stepper_get_position() == 0);
}

TEST(test_set_position) {
    reset_mocks();
    config_load();
    stepper_init();

    stepper_set_position(100);
    assert(stepper_get_position() == 100);

    stepper_set_position(-500);
    assert(stepper_get_position() == -500);
}

TEST(test_delay_60khz) {
    reset_mocks();
    config_load();
    stepper_init();

    // 60 kHz @ 133 MHz:
    // total_cycles = 133000000 / 60000 = 2216
    // pulse_cycles = 2.5 * 133 = 332
    // delay = 2216 - 332 - 5 = 1879
    uint32_t delay = stepper_delay_from_rate(60000.0f);
    // Allow ±5 for float rounding
    assert(delay >= 1874 && delay <= 1884);
}

TEST(test_delay_1khz) {
    reset_mocks();
    config_load();
    stepper_init();

    // 1 kHz @ 133 MHz:
    // total_cycles = 133000000 / 1000 = 133000
    // pulse_cycles = 332
    // delay = 133000 - 332 - 5 = 132663
    uint32_t delay = stepper_delay_from_rate(1000.0f);
    assert(delay >= 132658 && delay <= 132668);
}

TEST(test_delay_zero_rate) {
    reset_mocks();
    config_load();
    stepper_init();

    uint32_t delay = stepper_delay_from_rate(0.0f);
    assert(delay == UINT32_MAX);

    delay = stepper_delay_from_rate(-100.0f);
    assert(delay == UINT32_MAX);
}

TEST(test_fifo_free_count) {
    reset_mocks();
    config_load();
    stepper_init();

    assert(stepper_fifo_free() == 4);

    stepper_push_step(1000);
    assert(stepper_fifo_free() == 3);

    stepper_push_step(1000);
    stepper_push_step(1000);
    assert(stepper_fifo_free() == 1);
}

int main(void) {
    printf("Stepper tests:\n");
    RUN(test_init_starts_disabled);
    RUN(test_enable_disable);
    RUN(test_direction_forward);
    RUN(test_direction_reverse);
    RUN(test_push_step_forward);
    RUN(test_push_step_reverse);
    RUN(test_direction_change);
    RUN(test_fifo_full_rejects);
    RUN(test_stop_clears);
    RUN(test_zero_position);
    RUN(test_set_position);
    RUN(test_delay_60khz);
    RUN(test_delay_1khz);
    RUN(test_delay_zero_rate);
    RUN(test_fifo_free_count);
    printf("All stepper tests passed!\n\n");
    return 0;
}
