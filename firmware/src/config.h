#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    // Spindle
    uint16_t spindle_ppr;
    uint8_t  spindle_quadrature;
    uint32_t spindle_counts_per_rev;
    uint16_t spindle_max_rpm;

    // Z axis
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
} machine_config_t;

// Get pointer to active config (read-only outside config module)
const machine_config_t *config_get_all(void);

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
