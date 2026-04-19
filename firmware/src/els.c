#include "els.h"
#include "els_fsm.h"
#include "spindle.h"
#include "config.h"
#include "els_engine.h"
#include "pico/stdlib.h"
#include <string.h>

static float s_pitch_mm = 0.0f;

void els_init(void) {
    s_pitch_mm = 0.0f;
    els_fsm_init();
}

void __not_in_flash_func(els_update)(void) {
    els_fsm_step();
    els_fsm_publish_status(spindle_read_rpm());
}

bool els_set_pitch(float pitch_mm) {
    if (pitch_mm <= 0.0f) return false;
    if (els_fsm_get_state() != ELS_STATE_IDLE) return false;
    // Reject if worst-case step rate (at max RPM) would exceed hardware limit
    const machine_config_t *cfg = config_get_all();
    float max_steps_per_sec = pitch_mm * cfg->z_steps_per_mm
                              * (float)cfg->spindle_max_rpm / 60.0f;
    if (max_steps_per_sec > (float)cfg->z_max_step_rate) return false;
    s_pitch_mm = pitch_mm;
    return true;
}

float els_get_pitch(void) {
    return s_pitch_mm;
}

bool els_engage(void) {
    if (s_pitch_mm <= 0.0f) return false;
    if (els_fsm_get_state() != ELS_STATE_IDLE) return false;

    // Use first matching thread table entry, or synthesize ratio from pitch
    const machine_config_t *cfg = config_get_all();
    for (uint16_t i = 0; i < cfg->thread_table_count; i++) {
        if (cfg->thread_table[i].pitch_mm == s_pitch_mm) {
            els_fsm_event(CMD_ARM_THREADING, i);
            return els_fsm_get_state() != ELS_STATE_FAULT;
        }
    }

    // Fallback: synthesize ratio directly from pitch
    int64_t num = (int64_t)(s_pitch_mm * 10000.0f + 0.5f)
                  * cfg->z_steps_per_rev;
    int64_t den = (int64_t)(cfg->z_leadscrew_pitch_mm * 10000.0f + 0.5f)
                  * cfg->spindle_counts_per_rev;
    return els_arm_threading(num, den, 1, 0);
}

void els_disengage(void) {
    els_fsm_event(CMD_DISENGAGE, 0);
}

void els_feed_hold(void) {
    els_fsm_event(CMD_FEED_HOLD, 0);
}

void els_resume(void) {
    els_fsm_event(CMD_RESUME, 0);
}

els_state_t els_get_state(void) {
    return els_fsm_get_state();
}

int32_t els_get_error(void) {
    const els_status_t *s = els_fsm_status_read();
    return s ? s->z_backlog : 0;
}

bool els_arm_threading(int64_t ratio_num, int64_t ratio_den,
                       uint8_t starts, uint8_t start_index) {
    if (ratio_num <= 0 || ratio_den <= 0) return false;
    if (els_fsm_get_state() != ELS_STATE_IDLE) return false;

    // Populate dynamic slot at the end of the thread table (or slot 0 if empty)
    machine_config_t *cfg = config_get_mutable();
    uint16_t slot = (cfg->thread_table_count < 64)
                    ? cfg->thread_table_count : 63;
    cfg->thread_table[slot].pitch_mm  = s_pitch_mm;
    cfg->thread_table[slot].ratio_num = ratio_num;
    cfg->thread_table[slot].ratio_den = ratio_den;
    cfg->thread_table[slot].starts    = starts ? starts : 1;
    if (slot == cfg->thread_table_count && cfg->thread_table_count < 64)
        cfg->thread_table_count++;

    // Configure starts + offset before arming so CMD_ARM_THREADING reads them
    uint32_t starts_payload = ((uint32_t)(starts ? starts : 1u) << 8)
                              | (uint32_t)(start_index & 0xFFu);
    els_fsm_event(CMD_SET_STARTS, starts_payload);
    els_fsm_event(CMD_ARM_THREADING, slot);
    return els_fsm_get_state() != ELS_STATE_FAULT;
}

bool els_start_feed(uint32_t feed_um_per_rev, uint8_t axes) {
    uint32_t payload = ((feed_um_per_rev & 0xFFFF) << 8) | (axes & 0xFF);
    els_fsm_event(CMD_START_FEED, payload);
    return els_fsm_get_state() != ELS_STATE_FAULT;
}

bool els_jog_start(stepper_axis_t axis, int8_t direction) {
    uint32_t payload = ((uint32_t)(axis & 0x03) << 4) | (direction > 0 ? 1u : 0u);
    els_fsm_event(CMD_JOG_START, payload);
    return els_fsm_get_state() != ELS_STATE_FAULT;
}

void els_jog_stop(stepper_axis_t axis) {
    (void)axis;
    els_fsm_event(CMD_JOG_STOP, 0);
}

bool els_index_to(uint16_t angle_tenths) {
    els_fsm_event(CMD_INDEX_TO, (uint32_t)angle_tenths);
    return els_fsm_get_state() != ELS_STATE_FAULT;
}

void els_reset_fault(void) {
    els_fsm_event(CMD_RESET_FAULT, 0);
}

const els_status_t *els_status_read(void) {
    return els_fsm_status_read();
}
