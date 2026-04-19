// Unit tests for els_fsm — state transitions, fault stickiness, FAULT reset.
// These tests drive the FSM via els_fsm_event (synchronous, no inter-core FIFO).
// Compile: gcc -Wall -std=c11 -I mocks -I ../src -o test_fsm \
//          test_fsm.c ../src/els_fsm.c ../src/els_engine.c ../src/els_ramp.c \
//          ../src/config.c mock_flash.c -lm

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "els_fsm.h"
#include "config.h"
#include "hardware/flash.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

// ---- Stubs ----

// Spindle stub
static int32_t  stub_count = 0;
static int8_t   stub_dir   = 0;
static uint32_t stub_rate  = 1000;
static bool     stub_index = false;
static bool     stub_index_fault_flag = false;

int32_t  spindle_update(void)        { return 0; }
int32_t  spindle_read_count(void)    { return stub_count; }
uint32_t spindle_read_rate_eps(void) { return stub_rate; }
int8_t   spindle_direction(void)     { return stub_dir; }
bool     spindle_index_latched(void) { return stub_index; }
void     spindle_index_latch_clear(void) { stub_index = false; }
bool     spindle_index_fault(void)   { return stub_index_fault_flag; }
void     spindle_arm_start_offset(uint32_t p) { (void)p; }
float    spindle_read_rpm(void)      { return 300.0f; }
void     spindle_init(void)          {}

// Stepper stub
bool stepper_push(stepper_axis_t a, uint32_t d) { (void)a;(void)d; return true; }
void stepper_set_dir(stepper_axis_t a, bool f)  { (void)a;(void)f; }
void stepper_enable(stepper_axis_t a, bool e)   { (void)a;(void)e; }
void stepper_stop(stepper_axis_t a)             { (void)a; }
uint stepper_fifo_free(stepper_axis_t a)        { (void)a; return 4; }

// Safety stub
bool safety_estop_active(void) { return false; }

// Multicore stub
bool multicore_fifo_rvalid(void) { return false; }
uint32_t multicore_fifo_pop_blocking(void) { return 0; }

#define TEST(name) static void name(void)
#define RUN(name)  do { printf("  " #name "..."); name(); printf(" OK\n"); } while(0)

static void reset_fsm(void) {
    memset(_mock_flash, 0xFF, sizeof(_mock_flash));
    config_load();
    // Populate minimal thread table entry
    machine_config_t *cfg = (machine_config_t *)config_get_all();
    cfg->thread_table_count = 1;
    cfg->thread_table[0].pitch_mm  = 1.0f;
    cfg->thread_table[0].ratio_num = 1;
    cfg->thread_table[0].ratio_den = 1;
    cfg->thread_table[0].starts    = 1;
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

    stub_index = false;
    stub_index_fault_flag = false;
    stub_count = 0;
    els_fsm_init();
}

// IDLE on boot
TEST(test_initial_state_is_idle) {
    reset_fsm();
    assert(els_fsm_get_state() == ELS_STATE_IDLE);
    assert(els_fsm_get_fault() == ELS_FAULT_NONE);
}

// IDLE → THREADING_ARMED via CMD_ARM_THREADING
TEST(test_arm_threading_transitions) {
    reset_fsm();
    els_fsm_event(CMD_ARM_THREADING, 0);
    assert(els_fsm_get_state() == ELS_STATE_THREADING_ARMED);
}

// ARMED → ENGAGED when index fires
TEST(test_index_releases_armed) {
    reset_fsm();
    els_fsm_event(CMD_ARM_THREADING, 0);
    stub_index = true;
    els_fsm_step();
    assert(els_fsm_get_state() == ELS_STATE_THREADING_ENGAGED);
}

// THREADING_ENGAGED → THREADING_HOLD on CMD_FEED_HOLD
TEST(test_feed_hold_transitions) {
    reset_fsm();
    els_fsm_event(CMD_ARM_THREADING, 0);
    stub_index = true;
    els_fsm_step();
    els_fsm_event(CMD_FEED_HOLD, 0);
    assert(els_fsm_get_state() == ELS_STATE_THREADING_HOLD);
}

// THREADING_HOLD → THREADING_ENGAGED on CMD_RESUME
TEST(test_resume_from_hold) {
    reset_fsm();
    els_fsm_event(CMD_ARM_THREADING, 0);
    stub_index = true;
    els_fsm_step();
    els_fsm_event(CMD_FEED_HOLD, 0);
    els_fsm_event(CMD_RESUME, 0);
    assert(els_fsm_get_state() == ELS_STATE_THREADING_ENGAGED);
}

// THREADING_ENGAGED → IDLE on CMD_DISENGAGE
TEST(test_disengage_returns_idle) {
    reset_fsm();
    els_fsm_event(CMD_ARM_THREADING, 0);
    stub_index = true;
    els_fsm_step();
    els_fsm_event(CMD_DISENGAGE, 0);
    assert(els_fsm_get_state() == ELS_STATE_IDLE);
}

// FAULT is sticky — unknown commands don't clear it
TEST(test_fault_is_sticky) {
    reset_fsm();
    els_fsm_event(CMD_ARM_THREADING, 0);
    stub_index = true;
    els_fsm_step();
    stub_index_fault_flag = true;
    els_fsm_step();
    assert(els_fsm_get_state() == ELS_STATE_FAULT);

    // Random commands should not change state
    els_fsm_event(CMD_ARM_THREADING, 0);
    els_fsm_event(CMD_DISENGAGE, 0);
    assert(els_fsm_get_state() == ELS_STATE_FAULT);
}

// FAULT → IDLE on CMD_RESET_FAULT only
TEST(test_reset_fault_clears) {
    reset_fsm();
    els_fsm_event(CMD_ARM_THREADING, 0);
    stub_index = true;
    els_fsm_step();
    stub_index_fault_flag = true;
    els_fsm_step();
    assert(els_fsm_get_state() == ELS_STATE_FAULT);

    els_fsm_event(CMD_RESET_FAULT, 0);
    assert(els_fsm_get_state() == ELS_STATE_IDLE);
    assert(els_fsm_get_fault() == ELS_FAULT_NONE);
}

// Bad thread table index → FAULT
TEST(test_bad_table_index_faults) {
    reset_fsm();
    els_fsm_event(CMD_ARM_THREADING, 99); // out of range
    assert(els_fsm_get_state() == ELS_STATE_FAULT);
    assert(els_fsm_get_fault() == ELS_FAULT_BAD_RATIO);
}

// IDLE → JOG, JOG → IDLE after stop
TEST(test_jog_start_stop) {
    reset_fsm();
    uint32_t payload = ((uint32_t)AXIS_Z << 4) | 1u;
    els_fsm_event(CMD_JOG_START, payload);
    assert(els_fsm_get_state() == ELS_STATE_JOG);
    els_fsm_event(CMD_JOG_STOP, 0);
    // Ramp must decelerate to IDLE; run steps until idle
    for (int i = 0; i < 10000; i++) {
        if (els_fsm_get_state() == ELS_STATE_IDLE) break;
        els_fsm_step();
    }
    assert(els_fsm_get_state() == ELS_STATE_IDLE);
}

int main(void) {
    printf("=== test_fsm ===\n");
    RUN(test_initial_state_is_idle);
    RUN(test_arm_threading_transitions);
    RUN(test_index_releases_armed);
    RUN(test_feed_hold_transitions);
    RUN(test_resume_from_hold);
    RUN(test_disengage_returns_idle);
    RUN(test_fault_is_sticky);
    RUN(test_reset_fault_clears);
    RUN(test_bad_table_index_faults);
    RUN(test_jog_start_stop);
    printf("All passed.\n");
    return 0;
}
