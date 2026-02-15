#include "els.h"
#include "encoder.h"
#include "stepper.h"
#include "config.h"

// Error threshold in steps (~0.3mm at 166 steps/mm)
#define ELS_ERROR_THRESHOLD 50

// State
static els_state_t s_state = ELS_IDLE;
static float s_pitch_mm = 0.0f;

// Integer ratio: z_target = spindle_delta * s_ratio_num / s_ratio_den
static int64_t s_ratio_num = 0;
static int64_t s_ratio_den = 1;

// Origins snapped at engage (relative positioning avoids int32 overflow)
static int32_t s_spindle_origin = 0;
static int32_t s_stepper_origin = 0;

// Tracking error
static int32_t s_error = 0;

void els_init(void) {
    s_state = ELS_IDLE;
    s_pitch_mm = 0.0f;
    s_ratio_num = 0;
    s_ratio_den = 1;
    s_spindle_origin = 0;
    s_stepper_origin = 0;
    s_error = 0;
}

bool els_set_pitch(float pitch_mm) {
    if (pitch_mm <= 0.0f) return false;
    if (s_state != ELS_IDLE) return false;

    const machine_config_t *cfg = config_get_all();

    // Validate: at max RPM, would step frequency exceed stepper driver limit?
    // CL57T practical limit ~200 kHz; z_max_speed_mm_s is for jog, not threading.
    float max_step_rate = pitch_mm * cfg->z_steps_per_mm *
                          (float)cfg->spindle_max_rpm / 60.0f;
    if (max_step_rate > 200000.0f) return false;

    s_pitch_mm = pitch_mm;

    // Precompute integer ratio for sync loop:
    // ratio = (pitch × z_steps_per_rev × z_belt_ratio) / (z_leadscrew_pitch × spindle_counts_per_rev)
    // Convert floats to scaled integers to avoid FP in the hot path.
    int64_t pitch_x10000 = (int64_t)(pitch_mm * 10000.0f + 0.5f);
    int64_t lead_x10000  = (int64_t)(cfg->z_leadscrew_pitch_mm * 10000.0f + 0.5f);
    int64_t belt_x1000   = (int64_t)(cfg->z_belt_ratio * 1000.0f + 0.5f);

    s_ratio_num = pitch_x10000 * (int64_t)cfg->z_steps_per_rev * belt_x1000;
    s_ratio_den = lead_x10000  * (int64_t)cfg->spindle_counts_per_rev * 1000LL;

    return true;
}

float els_get_pitch(void) {
    return s_pitch_mm;
}

bool els_engage(void) {
    if (s_state != ELS_IDLE) return false;
    if (s_pitch_mm <= 0.0f) return false;

    // Snap origins
    s_spindle_origin = spindle_read_count();
    s_stepper_origin = stepper_get_position();
    s_error = 0;

    // Enable stepper driver
    stepper_enable(true);
    stepper_set_dir(true);

    s_state = ELS_ENGAGED;
    return true;
}

void els_disengage(void) {
    if (s_state != ELS_IDLE) {
        stepper_stop();
        stepper_enable(false);
    }
    s_error = 0;
    s_state = ELS_IDLE;
}

void els_feed_hold(void) {
    if (s_state != ELS_ENGAGED) return;

    // Stop pushing steps; PIO blocks at pull, motor holds with torque
    stepper_stop();
    s_state = ELS_FEED_HOLD;
}

void els_resume(void) {
    if (s_state != ELS_FEED_HOLD) return;

    // Re-snap origins to prevent lurch (spindle moved during hold)
    s_spindle_origin = spindle_read_count();
    s_stepper_origin = stepper_get_position();
    s_error = 0;

    s_state = ELS_ENGAGED;
}

els_state_t els_get_state(void) {
    return s_state;
}

int32_t els_get_error(void) {
    return s_error;
}

void els_update(void) {
    if (s_state != ELS_ENGAGED) return;

    // Compute spindle delta from engage origin
    int32_t spindle_delta = spindle_read_count() - s_spindle_origin;

    // Target Z position in steps (integer math)
    int64_t z_target = (int64_t)spindle_delta * s_ratio_num / s_ratio_den;

    // Actual Z position relative to engage origin
    int32_t z_actual = stepper_get_position() - s_stepper_origin;

    // Tracking error
    s_error = (int32_t)(z_target - (int64_t)z_actual);

    // If error too large, stop pushing (main.c handles alarm)
    if (s_error > ELS_ERROR_THRESHOLD || s_error < -ELS_ERROR_THRESHOLD)
        return;

    if (s_error == 0) return;

    // Set direction from error sign
    bool want_fwd = (s_error > 0);
    if (want_fwd != stepper_get_dir())
        stepper_set_dir(want_fwd);

    // Step rate from current RPM
    float rpm = spindle_read_rpm();
    if (rpm < 1.0f) return;  // Spindle stopped

    const machine_config_t *cfg = config_get_all();
    float step_rate = (rpm / 60.0f) * s_pitch_mm * cfg->z_steps_per_mm;
    uint32_t delay = stepper_delay_from_rate(step_rate);

    // Push one step per iteration
    if (stepper_fifo_free() > 0)
        stepper_push_step(delay);
}
