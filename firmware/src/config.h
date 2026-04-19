#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    // Spindle
    uint16_t spindle_ppr;
    uint8_t  spindle_quadrature;
    uint32_t spindle_counts_per_rev;
    uint16_t spindle_max_rpm;

    // Z axis — scale (DRO) + stepper (ELS)
    float    z_scale_resolution_mm;
    float    z_leadscrew_pitch_mm;
    uint16_t z_steps_per_rev;
    float    z_belt_ratio;
    float    z_steps_per_mm;            // derived
    float    z_max_speed_mm_s;
    float    z_accel_mm_s2;
    float    z_backlash_mm;
    float    z_travel_min_mm;
    float    z_travel_max_mm;

    // X axis
    float    x_scale_resolution_mm;
    bool     x_is_diameter;
    float    x_travel_min_mm;
    float    x_travel_max_mm;

    // Future X stepper
    uint16_t x_steps_per_rev;
    float    x_leadscrew_pitch_mm;
    float    x_belt_ratio;
    float    x_steps_per_mm;            // derived

    // Threading
    uint8_t  thread_retract_mode;       // 0 = rapid retract X, 1 = spring pass
    float    thread_retract_x_mm;
    float    thread_compound_angle;

    // ELS — per-axis max step rates (steps/sec)
    uint32_t z_max_step_rate;           // default: 200000
    uint32_t x_max_step_rate;           // default: 200000
    uint32_t c_max_step_rate;           // default: 10000 (VFD opto limit)

    // ELS — per-axis soft limits (in steps; INT32_MIN/MAX = disabled)
    int32_t  z_soft_min_steps;
    int32_t  z_soft_max_steps;
    int32_t  x_soft_min_steps;
    int32_t  x_soft_max_steps;

    // ELS — backlog fault thresholds (steps)
    uint32_t z_backlog_threshold;       // default: 16
    uint32_t x_backlog_threshold;       // default: 16

    // ELS — ramp parameters (PIO cycles at 125 MHz)
    uint32_t z_ramp_min_delay;          // derived from z_max_step_rate at recalculate
    uint32_t z_ramp_max_delay;          // default: 125000 (~1 step/ms)
    uint32_t z_ramp_delta;              // default: 500 (delay change per step)
    uint32_t x_ramp_min_delay;
    uint32_t x_ramp_max_delay;
    uint32_t x_ramp_delta;
    uint32_t c_ramp_min_delay;          // derived from c_max_step_rate
    uint32_t c_ramp_delta;              // default: 2000

    // ELS — C-axis (VFD) calibration
    float    c_steps_per_rev;           // default: 10000
    float    c_pulse_width_us;          // default: 5.0 µs (VFD opto min)

    // ELS — thread table (precomputed GCD-reduced ratios)
    uint16_t thread_table_count;
    struct {
        float    pitch_mm;
        int64_t  ratio_num;
        int64_t  ratio_den;
        uint8_t  starts;
    } thread_table[64];
} machine_config_t;

// Get pointer to active config (read-only outside config module)
const machine_config_t *config_get_all(void);

// Mutable access for ELS dynamic thread-table updates only
machine_config_t *config_get_mutable(void);

// Key-value access for serial protocol
bool config_get(const char *key, char *value_out, size_t value_len);
bool config_set(const char *key, const char *value);

// Flash persistence
bool config_save(void);
bool config_load(void);

// Recalculate derived values (z_steps_per_mm, x_steps_per_mm)
void config_recalculate(void);

// Enumerate all config keys for config_list command
typedef void (*config_list_cb)(const char *key, const char *value, void *ctx);
void config_list(config_list_cb cb, void *ctx);

#endif // CONFIG_H
