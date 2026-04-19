#include "els_fsm.h"
#include "els_engine.h"
#include "els_ramp.h"
#include "spindle.h"
#include "stepper.h"
#include "safety.h"
#include "config.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include <limits.h>
#include <string.h>

// ---- State ----

static els_state_t      s_state = ELS_STATE_IDLE;
static els_fault_code_t s_fault = ELS_FAULT_NONE;

// Per-axis engine + ramp
static els_axis_state_t s_engine[AXIS_COUNT];
static els_ramp_state_t s_ramp[AXIS_COUNT];

// Current spindle delta (updated each els_fsm_step call from spindle_update caller)
static int32_t  s_spindle_delta  = 0;
static uint32_t s_spindle_rate   = 0;

// Threading config
static uint8_t  s_starts      = 1;
static uint8_t  s_start_idx   = 0;

// Feed config
static uint32_t s_feed_um_rev = 0;
static uint8_t  s_feed_axes   = 0; // bitmask: bit0=Z, bit1=X

// Jog config
static stepper_axis_t s_jog_axis = AXIS_Z;

// Indexing config
static int32_t s_index_target = 0;
static int32_t s_index_c_start = 0;

// Double-buffered status
static els_status_t s_status_bufs[2];
static volatile els_status_t *s_status_front = &s_status_bufs[0];
static els_status_t *s_status_back  = &s_status_bufs[1];

// ---- Helpers ----

static void __not_in_flash_func(enter_fault)(els_fault_code_t code) {
    s_state = ELS_STATE_FAULT;
    s_fault = code;
    stepper_stop(AXIS_Z);
    stepper_stop(AXIS_X);
    stepper_stop(AXIS_C);
    stepper_enable(AXIS_Z, false);
    stepper_enable(AXIS_X, false);
}

static bool __not_in_flash_func(advance_axis)(stepper_axis_t ax_id) {
    els_axis_state_t *ax  = &s_engine[ax_id];
    els_ramp_state_t *rmp = &s_ramp[ax_id];

    if (!els_engine_axis_advance(ax, s_spindle_delta, s_spindle_rate)) {
        return false;  // backlog fault
    }
    if (els_engine_axis_backlog_fault(ax)) {
        return false;
    }

    uint32_t eff = els_ramp_floor(rmp, ax->predicted_delay);
    int32_t pushed = els_engine_axis_flush(ax, eff, stepper_fifo_free(ax_id));
    return pushed >= 0;  // -1 = soft limit fault
}

static void __not_in_flash_func(process_command)(uint32_t word) {
    els_cmd_opcode_t op = (els_cmd_opcode_t)(word >> 24);
    uint32_t payload    = word & 0x00FFFFFFu;

    const machine_config_t *cfg = config_get_all();

    switch (op) {
    case CMD_ARM_THREADING: {
        if (s_state != ELS_STATE_IDLE) break;
        uint8_t tidx = (uint8_t)(payload & 0xFF);
        if (tidx >= cfg->thread_table_count) {
            enter_fault(ELS_FAULT_BAD_RATIO);
            break;
        }
        int64_t num = cfg->thread_table[tidx].ratio_num;
        int64_t den = cfg->thread_table[tidx].ratio_den;
        s_starts    = cfg->thread_table[tidx].starts;

        if (cfg->spindle_counts_per_rev % s_starts != 0) {
            enter_fault(ELS_FAULT_MULTISTART_PPR);
            break;
        }
        if (!els_engine_axis_init(&s_engine[AXIS_Z], AXIS_Z, num, den,
                                   cfg->z_soft_min_steps, cfg->z_soft_max_steps,
                                   (int32_t)cfg->z_backlog_threshold)) {
            enter_fault(ELS_FAULT_BAD_RATIO);
            break;
        }

        uint32_t offset_pulses = (uint32_t)(s_start_idx *
            (cfg->spindle_counts_per_rev / s_starts));
        spindle_arm_start_offset(offset_pulses);

        els_ramp_engage(&s_ramp[AXIS_Z]);
        stepper_enable(AXIS_Z, true);
        s_state = ELS_STATE_THREADING_ARMED;
        break;
    }
    case CMD_SET_STARTS:
        s_starts    = (uint8_t)((payload >> 8) & 0xFF);
        s_start_idx = (uint8_t)(payload & 0xFF);
        break;

    case CMD_START_FEED: {
        if (s_state != ELS_STATE_IDLE) break;
        s_feed_um_rev = (payload >> 8) & 0xFFFF;
        s_feed_axes   = payload & 0xFF;

        if (s_feed_axes & 0x01) {
            int64_t num = (int64_t)s_feed_um_rev * cfg->z_steps_per_mm;
            int64_t den = 1000LL * cfg->spindle_counts_per_rev;
            if (!els_engine_axis_init(&s_engine[AXIS_Z], AXIS_Z, num, den,
                                       cfg->z_soft_min_steps, cfg->z_soft_max_steps,
                                       (int32_t)cfg->z_backlog_threshold)) {
                enter_fault(ELS_FAULT_BAD_RATIO); break;
            }
            els_engine_axis_reset(&s_engine[AXIS_Z]);
            els_ramp_engage(&s_ramp[AXIS_Z]);
            stepper_enable(AXIS_Z, true);
        }
        if (s_feed_axes & 0x02) {
            int64_t num = (int64_t)s_feed_um_rev * cfg->x_steps_per_mm;
            int64_t den = 1000LL * cfg->spindle_counts_per_rev;
            if (!els_engine_axis_init(&s_engine[AXIS_X], AXIS_X, num, den,
                                       cfg->x_soft_min_steps, cfg->x_soft_max_steps,
                                       (int32_t)cfg->x_backlog_threshold)) {
                enter_fault(ELS_FAULT_BAD_RATIO); break;
            }
            els_engine_axis_reset(&s_engine[AXIS_X]);
            els_ramp_engage(&s_ramp[AXIS_X]);
            stepper_enable(AXIS_X, true);
        }
        s_state = ELS_STATE_FEED_ENGAGED;
        break;
    }
    case CMD_JOG_START: {
        if (s_state != ELS_STATE_IDLE) break;
        s_jog_axis      = (stepper_axis_t)((payload >> 4) & 0x03);
        bool dir        = (payload & 0x01) != 0;
        stepper_enable(s_jog_axis, true);
        stepper_set_dir(s_jog_axis, dir);
        els_ramp_engage(&s_ramp[s_jog_axis]);
        s_state = ELS_STATE_JOG;
        break;
    }
    case CMD_JOG_STOP:
        if (s_state == ELS_STATE_JOG) {
            els_ramp_disengage(&s_ramp[s_jog_axis]);
        }
        break;

    case CMD_INDEX_TO: {
        if (s_state != ELS_STATE_IDLE) break;
        const machine_config_t *c = config_get_all();
        uint16_t angle_tenths = (uint16_t)(payload & 0xFFFF);
        // target in encoder counts = angle_tenths × PPR × 4 / 3600
        s_index_target = (int32_t)(
            (uint64_t)angle_tenths * c->spindle_counts_per_rev / 3600u);
        s_index_c_start = spindle_read_count();
        stepper_enable(AXIS_C, true);
        els_ramp_engage(&s_ramp[AXIS_C]);
        s_state = ELS_STATE_INDEXING;
        break;
    }
    case CMD_DISARM:
        if (s_state == ELS_STATE_THREADING_ARMED) {
            els_ramp_disengage(&s_ramp[AXIS_Z]);
            stepper_enable(AXIS_Z, false);
            s_state = ELS_STATE_IDLE;
        }
        break;

    case CMD_DISENGAGE:
        if (s_state == ELS_STATE_THREADING_ARMED   ||
            s_state == ELS_STATE_THREADING_ENGAGED ||
            s_state == ELS_STATE_THREADING_HOLD    ||
            s_state == ELS_STATE_FEED_ENGAGED      ||
            s_state == ELS_STATE_TAPER_ENGAGED) {
            els_ramp_disengage(&s_ramp[AXIS_Z]);
            els_ramp_disengage(&s_ramp[AXIS_X]);
            stepper_stop(AXIS_Z);
            stepper_stop(AXIS_X);
            stepper_enable(AXIS_Z, false);
            stepper_enable(AXIS_X, false);
            s_state = ELS_STATE_IDLE;
        }
        break;

    case CMD_FEED_HOLD:
        if (s_state == ELS_STATE_THREADING_ENGAGED) {
            stepper_stop(AXIS_Z);
            s_state = ELS_STATE_THREADING_HOLD;
        }
        break;

    case CMD_RESUME:
        if (s_state == ELS_STATE_THREADING_HOLD) {
            els_engine_axis_reset(&s_engine[AXIS_Z]);
            els_ramp_engage(&s_ramp[AXIS_Z]);
            s_state = ELS_STATE_THREADING_ENGAGED;
        }
        break;

    case CMD_RESET_FAULT:
        if (s_state == ELS_STATE_FAULT) {
            s_fault = ELS_FAULT_NONE;
            s_state = ELS_STATE_IDLE;
        }
        break;

    default:
        break;
    }
}

// ---- Public API ----

void els_fsm_init(void) {
    const machine_config_t *cfg = config_get_all();

    s_state = ELS_STATE_IDLE;
    s_fault = ELS_FAULT_NONE;

    for (int i = 0; i < AXIS_COUNT; i++) {
        uint32_t min_d, max_d, delta;
        switch (i) {
        case AXIS_Z:
            min_d = cfg->z_ramp_min_delay; max_d = cfg->z_ramp_max_delay;
            delta = cfg->z_ramp_delta; break;
        case AXIS_X:
            min_d = cfg->x_ramp_min_delay; max_d = cfg->x_ramp_max_delay;
            delta = cfg->x_ramp_delta; break;
        default: // AXIS_C
            min_d = cfg->c_ramp_min_delay; max_d = 2500000u;
            delta = cfg->c_ramp_delta; break;
        }
        els_ramp_init(&s_ramp[i], min_d, max_d, delta);
    }

    s_status_front = &s_status_bufs[0];
    s_status_back  = &s_status_bufs[1];
    memset(s_status_bufs, 0, sizeof(s_status_bufs));
}

void __not_in_flash_func(els_fsm_step)(void) {
    s_spindle_delta = spindle_update();
    s_spindle_rate  = spindle_read_rate_eps();

    // Poll inter-core FIFO for commands from Core 1
    while (multicore_fifo_rvalid()) {
        process_command(multicore_fifo_pop_blocking());
    }

    // E-stop guard (any state)
    if (safety_estop_active() && s_state != ELS_STATE_IDLE &&
                                  s_state != ELS_STATE_FAULT) {
        enter_fault(ELS_FAULT_ESTOP);
        return;
    }

    switch (s_state) {

    case ELS_STATE_THREADING_ARMED:
        // Detect reversal before index fires
        if (s_spindle_delta < 0 || spindle_index_fault()) {
            enter_fault(spindle_index_fault() ? ELS_FAULT_INDEX_LOST : ELS_FAULT_REVERSAL);
            break;
        }
        if (spindle_index_latched()) {
            spindle_index_latch_clear();
            els_engine_axis_reset(&s_engine[AXIS_Z]);
            s_state = ELS_STATE_THREADING_ENGAGED;
            // fall through to advance on this tick
        } else {
            break;
        }
        // fall through

    case ELS_STATE_THREADING_ENGAGED:
        if (spindle_index_fault()) {
            enter_fault(ELS_FAULT_INDEX_LOST);
            break;
        }
        if (s_spindle_delta < 0) {
            enter_fault(ELS_FAULT_REVERSAL);
            break;
        }
        if (!advance_axis(AXIS_Z)) {
            enter_fault(els_engine_axis_backlog_fault(&s_engine[AXIS_Z])
                        ? ELS_FAULT_BACKLOG : ELS_FAULT_SOFT_LIMIT);
        }
        break;

    case ELS_STATE_THREADING_HOLD:
        // Hold: no steps, spindle may be running
        break;

    case ELS_STATE_FEED_ENGAGED:
        // Tracks reversal (both directions)
        if (s_feed_axes & 0x01) {
            if (!advance_axis(AXIS_Z)) {
                enter_fault(els_engine_axis_backlog_fault(&s_engine[AXIS_Z])
                            ? ELS_FAULT_BACKLOG : ELS_FAULT_SOFT_LIMIT);
                break;
            }
        }
        if (s_feed_axes & 0x02) {
            if (!advance_axis(AXIS_X)) {
                enter_fault(els_engine_axis_backlog_fault(&s_engine[AXIS_X])
                            ? ELS_FAULT_BACKLOG : ELS_FAULT_SOFT_LIMIT);
                break;
            }
        }
        break;

    case ELS_STATE_TAPER_ENGAGED:
        if (!advance_axis(AXIS_Z) || !advance_axis(AXIS_X)) {
            enter_fault(ELS_FAULT_BACKLOG);
        }
        break;

    case ELS_STATE_JOG: {
        uint32_t delay = els_ramp_step(&s_ramp[s_jog_axis]);
        if (delay == UINT32_MAX) {
            // Ramp finished decelerating
            stepper_stop(s_jog_axis);
            stepper_enable(s_jog_axis, false);
            s_state = ELS_STATE_IDLE;
        } else {
            stepper_push(s_jog_axis, delay);
        }
        break;
    }

    case ELS_STATE_INDEXING: {
        if (spindle_index_fault()) {
            enter_fault(ELS_FAULT_INDEX_LOST);
            break;
        }
        int32_t current = spindle_read_count() - s_index_c_start;
        int32_t error   = s_index_target - current;

        if (error == 0) {
            // At target: hold (FIFO empty → PIO blocks, motor holds torque)
            break;
        }

        bool fwd = (error > 0);
        stepper_set_dir(AXIS_C, fwd);

        uint32_t delay = els_ramp_step(&s_ramp[AXIS_C]);
        if (delay != UINT32_MAX) {
            stepper_push(AXIS_C, delay);
        }
        break;
    }

    case ELS_STATE_IDLE:
    case ELS_STATE_FAULT:
        break;
    }
}

void __not_in_flash_func(els_fsm_event)(els_cmd_opcode_t cmd, uint32_t payload) {
    uint32_t word = ((uint32_t)cmd << 24) | (payload & 0x00FFFFFFu);
    process_command(word);
}

els_state_t els_fsm_get_state(void) {
    return s_state;
}

els_fault_code_t els_fsm_get_fault(void) {
    return s_fault;
}

void __not_in_flash_func(els_fsm_publish_status)(float spindle_rpm) {
    s_status_back->state         = s_state;
    s_status_back->fault         = s_fault;
    s_status_back->z_pos_steps   = s_engine[AXIS_Z].position;
    s_status_back->x_pos_steps   = s_engine[AXIS_X].position;
    s_status_back->c_pos_steps   = s_engine[AXIS_C].position;
    s_status_back->spindle_count = spindle_read_count();
    s_status_back->spindle_rpm   = spindle_rpm;
    s_status_back->z_backlog     = s_engine[AXIS_Z].backlog;
    s_status_back->x_backlog     = s_engine[AXIS_X].backlog;
    s_status_back->index_latched = spindle_index_latched();
    s_status_back->estop         = safety_estop_active();

    // Atomic pointer swap: publish back buffer
    els_status_t *tmp = (els_status_t *)s_status_front;
    s_status_front = s_status_back;
    s_status_back  = tmp;
}

const els_status_t *els_fsm_status_read(void) {
    return (const els_status_t *)s_status_front;
}
