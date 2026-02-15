// Unit tests for els.c — Electronic Leadscrew control loop
// Compile with: gcc -I mocks -I ../src -o test_els test_els.c ../src/els.c ../src/stepper.c ../src/encoder.c ../src/config.c mock_flash.c -lm

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include "els.h"
#include "encoder.h"
#include "stepper.h"
#include "config.h"
#include "pins.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "quadrature.pio.h"
#include "stepper.pio.h"

#define TEST(name) static void name(void)
#define RUN(name) do { printf("  " #name "..."); name(); printf(" OK\n"); } while(0)

static void reset_mocks(void) {
    _mock_time_us = 0;
    for (int i = 0; i < 4; i++) {
        mock_tx_fifo_clear(i);
        mock_fifo_clear(i);
        s_counts[i] = 0;
        s_prev_state[i] = 0;
    }
    memset(_mock_gpio_state, 0, sizeof(_mock_gpio_state));
}

static void init_all(void) {
    config_load();
    config_recalculate();
    encoder_init();
    stepper_init();
    els_init();
}

// Set up RPM reading: simulates spindle rotation over a 50ms window
static void simulate_rpm(float rpm) {
    const machine_config_t *cfg = config_get_all();
    // RPM = (delta / counts_per_rev) / dt_sec * 60
    // delta = RPM * counts_per_rev * dt_sec / 60
    int32_t delta = (int32_t)(rpm * (float)cfg->spindle_counts_per_rev * 0.05f / 60.0f);

    mock_set_time_us(0);
    s_counts[0] = 0;
    encoder_update();

    mock_set_time_us(50000);
    s_counts[0] = delta;
    encoder_update();
}

// Simulate PIO consuming TX FIFO entries (mock FIFO never drains on its own)
static void drain_tx_fifo(void) {
    _mock_tx_fifo_count[0] = 0;
}

// ── State machine tests ─────────────────────────────────────────────

TEST(test_init_state_idle) {
    reset_mocks();
    init_all();

    assert(els_get_state() == ELS_IDLE);
    assert(els_get_pitch() == 0.0f);
    assert(els_get_error() == 0);
}

TEST(test_set_pitch_valid) {
    reset_mocks();
    init_all();

    assert(els_set_pitch(1.75f));
    assert(fabsf(els_get_pitch() - 1.75f) < 0.001f);
}

TEST(test_set_pitch_zero_rejected) {
    reset_mocks();
    init_all();

    assert(!els_set_pitch(0.0f));
    assert(els_get_pitch() == 0.0f);
}

TEST(test_set_pitch_negative_rejected) {
    reset_mocks();
    init_all();

    assert(!els_set_pitch(-1.0f));
    assert(els_get_pitch() == 0.0f);
}

TEST(test_set_pitch_exceeds_max_speed) {
    reset_mocks();
    init_all();

    // 50mm pitch at 3500 RPM: 50 * 166.67 * 3500/60 = 486,113 Hz >> 200 kHz limit
    assert(!els_set_pitch(50.0f));
}

TEST(test_set_pitch_while_engaged) {
    reset_mocks();
    init_all();

    assert(els_set_pitch(1.0f));
    simulate_rpm(600.0f);
    assert(els_engage());

    // Can't change pitch while engaged
    assert(!els_set_pitch(2.0f));
    assert(fabsf(els_get_pitch() - 1.0f) < 0.001f);

    els_disengage();
}

TEST(test_engage_without_pitch) {
    reset_mocks();
    init_all();

    assert(!els_engage());
    assert(els_get_state() == ELS_IDLE);
}

TEST(test_engage_success) {
    reset_mocks();
    init_all();

    assert(els_set_pitch(1.75f));
    assert(els_engage());
    assert(els_get_state() == ELS_ENGAGED);

    // Stepper should be enabled (GP10 LOW = enabled, active low)
    assert(_mock_gpio_state[PIN_Z_ENABLE] == false);
}

TEST(test_engage_twice) {
    reset_mocks();
    init_all();

    assert(els_set_pitch(1.0f));
    assert(els_engage());
    assert(!els_engage());  // Second engage fails
    assert(els_get_state() == ELS_ENGAGED);

    els_disengage();
}

TEST(test_disengage) {
    reset_mocks();
    init_all();

    assert(els_set_pitch(1.0f));
    assert(els_engage());
    els_disengage();

    assert(els_get_state() == ELS_IDLE);
    // Stepper should be disabled (GP10 HIGH)
    assert(_mock_gpio_state[PIN_Z_ENABLE] == true);
}

TEST(test_disengage_from_idle) {
    reset_mocks();
    init_all();

    // Should not crash
    els_disengage();
    assert(els_get_state() == ELS_IDLE);
}

TEST(test_feed_hold) {
    reset_mocks();
    init_all();

    assert(els_set_pitch(1.0f));
    assert(els_engage());
    els_feed_hold();

    assert(els_get_state() == ELS_FEED_HOLD);
    // Stepper should still be enabled (holds position with torque)
    // Note: stepper_stop() resets the SM, but doesn't call stepper_enable(false)
    // The enable pin state depends on the last stepper_enable() call
    // which was true during engage
}

TEST(test_feed_hold_from_idle) {
    reset_mocks();
    init_all();

    els_feed_hold();  // No-op from idle
    assert(els_get_state() == ELS_IDLE);
}

TEST(test_resume) {
    reset_mocks();
    init_all();

    assert(els_set_pitch(1.0f));
    assert(els_engage());
    els_feed_hold();
    assert(els_get_state() == ELS_FEED_HOLD);

    els_resume();
    assert(els_get_state() == ELS_ENGAGED);

    els_disengage();
}

TEST(test_resume_from_idle) {
    reset_mocks();
    init_all();

    els_resume();  // No-op from idle
    assert(els_get_state() == ELS_IDLE);
}

// ── Sync loop tests ─────────────────────────────────────────────────

TEST(test_sync_1mm_pitch) {
    reset_mocks();
    init_all();

    assert(els_set_pitch(1.0f));

    // Set up RPM before engaging
    simulate_rpm(600.0f);

    // Engage — snaps origin at current spindle count
    int32_t origin = spindle_read_count();
    assert(els_engage());

    // Simulate gradual spindle rotation: 1 revolution = 4000 counts
    // At 600 RPM, 50kHz loop: ~0.8 counts per loop iteration
    // Advance 1 count per iteration to stay within error threshold
    for (int i = 1; i <= 4000; i++) {
        s_counts[0] = origin + i;
        encoder_update();
        drain_tx_fifo();
        els_update();
    }
    // Run a few extra iterations to flush remaining steps
    for (int i = 0; i < 50; i++) {
        drain_tx_fifo();
        els_update();
    }

    int32_t pos = stepper_get_position();
    // 1.0mm pitch, z_steps_per_mm = 166.67 → expect ~166 steps
    assert(pos >= 165 && pos <= 168);

    els_disengage();
}

TEST(test_sync_fractional_pitch) {
    reset_mocks();
    init_all();

    assert(els_set_pitch(1.75f));

    simulate_rpm(600.0f);

    int32_t origin = spindle_read_count();
    assert(els_engage());

    // Simulate gradual spindle rotation: 1 revolution = 4000 counts
    for (int i = 1; i <= 4000; i++) {
        s_counts[0] = origin + i;
        encoder_update();
        drain_tx_fifo();
        els_update();
    }
    for (int i = 0; i < 50; i++) {
        drain_tx_fifo();
        els_update();
    }

    int32_t pos = stepper_get_position();
    // 1.75mm × 166.67 steps/mm = 291.67 → expect ~291-292
    assert(pos >= 290 && pos <= 293);

    els_disengage();
}

TEST(test_sync_no_steps_at_zero_rpm) {
    reset_mocks();
    init_all();

    assert(els_set_pitch(1.0f));

    // Don't set up RPM — leave at 0
    s_counts[0] = 0;
    encoder_update();

    assert(els_engage());
    int32_t origin_pos = stepper_get_position();

    // Simulate spindle counts but RPM is still 0
    s_counts[0] = 1000;
    encoder_update();

    // Sync loop should not push steps because RPM < 1.0
    for (int i = 0; i < 100; i++) {
        els_update();
    }

    assert(stepper_get_position() == origin_pos);

    els_disengage();
}

TEST(test_error_reporting) {
    reset_mocks();
    init_all();

    assert(els_set_pitch(1.0f));

    simulate_rpm(600.0f);

    assert(els_engage());

    // Create a large spindle delta without enough els_update calls
    int32_t origin = spindle_read_count();
    s_counts[0] = origin + 40000;  // 10 revolutions
    encoder_update();

    // Call update once — this computes the error but can only push 1 step
    els_update();

    // Error should be large (target ~1667 steps, actual 0-1)
    int32_t err = els_get_error();
    assert(err > 50 || err < -50);  // Exceeds threshold

    els_disengage();
}

int main(void) {
    printf("ELS tests:\n");
    RUN(test_init_state_idle);
    RUN(test_set_pitch_valid);
    RUN(test_set_pitch_zero_rejected);
    RUN(test_set_pitch_negative_rejected);
    RUN(test_set_pitch_exceeds_max_speed);
    RUN(test_set_pitch_while_engaged);
    RUN(test_engage_without_pitch);
    RUN(test_engage_success);
    RUN(test_engage_twice);
    RUN(test_disengage);
    RUN(test_disengage_from_idle);
    RUN(test_feed_hold);
    RUN(test_feed_hold_from_idle);
    RUN(test_resume);
    RUN(test_resume_from_idle);
    RUN(test_sync_1mm_pitch);
    RUN(test_sync_fractional_pitch);
    RUN(test_sync_no_steps_at_zero_rpm);
    RUN(test_error_reporting);
    printf("All ELS tests passed!\n\n");
    return 0;
}
