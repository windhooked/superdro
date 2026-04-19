// Unit tests for els.c — ELS shim API (pitch validation, engage/disengage,
// feed-hold/resume state transitions). Sync correctness is in test_bresenham
// and test_fsm; hardware integration is in the bring-up doc.
//
// Compile: gcc -Wall -std=c11 -I mocks -I ../src -o test_els \
//          test_els.c ../src/els.c ../src/stepper.c ../src/config.c \
//          ../src/els_fsm.c ../src/els_engine.c ../src/els_ramp.c \
//          mock_flash.c mock_spindle.c -lm

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "els.h"
#include "els_fsm.h"
#include "config.h"
#include "hardware/flash.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

#define TEST(name) static void name(void)
#define RUN(name)  do { printf("  " #name "..."); name(); printf(" OK\n"); } while(0)

static void reset_all(void) {
    memset(_mock_flash, 0xFF, sizeof(_mock_flash));
    config_load();
    config_recalculate();
    // Populate a minimal thread table so els_engage can find pitch 1.0
    machine_config_t *cfg = config_get_mutable();
    cfg->thread_table_count = 2;
    cfg->thread_table[0].pitch_mm  = 1.0f;
    cfg->thread_table[0].ratio_num = 1;
    cfg->thread_table[0].ratio_den = 1;
    cfg->thread_table[0].starts    = 1;
    cfg->thread_table[1].pitch_mm  = 1.75f;
    cfg->thread_table[1].ratio_num = 7;
    cfg->thread_table[1].ratio_den = 4;
    cfg->thread_table[1].starts    = 1;
    cfg->spindle_counts_per_rev    = 4000;
    cfg->z_backlog_threshold       = 16;
    cfg->z_ramp_min_delay          = 625;
    cfg->z_ramp_max_delay          = 125000;
    cfg->z_ramp_delta              = 500;
    cfg->x_ramp_min_delay          = 625;
    cfg->x_ramp_max_delay          = 125000;
    cfg->x_ramp_delta              = 500;
    cfg->c_ramp_min_delay          = 12500;
    cfg->c_ramp_delta              = 2000;
    els_init();
}

// ─── API / pitch validation ───────────────────────────────────────────────

TEST(test_init_state_idle) {
    reset_all();
    assert(els_get_state() == ELS_IDLE);
    assert(els_get_pitch() == 0.0f);
    assert(els_get_error() == 0);
}

TEST(test_set_pitch_valid) {
    reset_all();
    assert(els_set_pitch(1.0f));
    assert(fabsf(els_get_pitch() - 1.0f) < 0.001f);
}

TEST(test_set_pitch_zero_rejected) {
    reset_all();
    assert(!els_set_pitch(0.0f));
    assert(els_get_pitch() == 0.0f);
}

TEST(test_set_pitch_negative_rejected) {
    reset_all();
    assert(!els_set_pitch(-1.0f));
    assert(els_get_pitch() == 0.0f);
}

TEST(test_set_pitch_exceeds_max_speed) {
    reset_all();
    // 50 mm pitch × z_steps_per_mm × spindle_max_rpm / 60 >> z_max_step_rate
    assert(!els_set_pitch(50.0f));
}

TEST(test_set_pitch_while_engaged) {
    reset_all();
    assert(els_set_pitch(1.0f));
    assert(els_engage());
    // Can't change pitch while armed/engaged
    assert(!els_set_pitch(2.0f));
    assert(fabsf(els_get_pitch() - 1.0f) < 0.001f);
    els_disengage();
}

// ─── State transitions ────────────────────────────────────────────────────

// After engage, FSM enters THREADING_ARMED (waiting for index), not yet ENGAGED
TEST(test_engage_goes_to_armed) {
    reset_all();
    assert(els_set_pitch(1.0f));
    assert(els_engage());
    assert(els_get_state() == ELS_STATE_THREADING_ARMED);
    els_disengage();
}

TEST(test_engage_without_pitch) {
    reset_all();
    assert(!els_engage());
    assert(els_get_state() == ELS_IDLE);
}

// Second engage from ARMED state → fails (not IDLE)
TEST(test_engage_twice_rejected) {
    reset_all();
    assert(els_set_pitch(1.0f));
    assert(els_engage());
    assert(!els_engage());
    els_disengage();
}

TEST(test_disengage_from_armed) {
    reset_all();
    assert(els_set_pitch(1.0f));
    assert(els_engage());
    els_disengage();
    assert(els_get_state() == ELS_IDLE);
}

TEST(test_disengage_from_idle) {
    reset_all();
    els_disengage();
    assert(els_get_state() == ELS_IDLE);
}

// Feed-hold from ARMED: FSM ignores it (no THREADING_ENGAGED yet)
TEST(test_feed_hold_from_idle) {
    reset_all();
    els_feed_hold();
    assert(els_get_state() == ELS_IDLE);
}

TEST(test_resume_from_idle) {
    reset_all();
    els_resume();
    assert(els_get_state() == ELS_IDLE);
}

// ─── Fault path ───────────────────────────────────────────────────────────

TEST(test_bad_table_index_faults) {
    reset_all();
    els_fsm_event(CMD_ARM_THREADING, 99);
    assert(els_get_state() == ELS_STATE_FAULT);
    els_fsm_event(CMD_RESET_FAULT, 0);
    assert(els_get_state() == ELS_IDLE);
}

// ─── Jog ─────────────────────────────────────────────────────────────────

TEST(test_jog_start_stop) {
    reset_all();
    uint32_t payload = ((uint32_t)AXIS_Z << 4) | 1u;
    els_jog_start(AXIS_Z, true);
    assert(els_get_state() == ELS_STATE_JOG);
    els_jog_stop(AXIS_Z);
    // Ramp decelerates; run FSM steps until idle
    for (int i = 0; i < 10000 && els_get_state() != ELS_IDLE; i++)
        els_fsm_step();
    assert(els_get_state() == ELS_IDLE);
    (void)payload;
}

int main(void) {
    printf("=== test_els ===\n");
    RUN(test_init_state_idle);
    RUN(test_set_pitch_valid);
    RUN(test_set_pitch_zero_rejected);
    RUN(test_set_pitch_negative_rejected);
    RUN(test_set_pitch_exceeds_max_speed);
    RUN(test_set_pitch_while_engaged);
    RUN(test_engage_goes_to_armed);
    RUN(test_engage_without_pitch);
    RUN(test_engage_twice_rejected);
    RUN(test_disengage_from_armed);
    RUN(test_disengage_from_idle);
    RUN(test_feed_hold_from_idle);
    RUN(test_resume_from_idle);
    RUN(test_bad_table_index_faults);
    RUN(test_jog_start_stop);
    printf("All passed.\n");
    return 0;
}
