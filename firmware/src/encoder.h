#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

typedef struct {
    int32_t raw_count;
    float   position_mm;
} axis_position_t;

// Initialize PIO state machines for spindle, X-axis, and Z-axis encoders
void encoder_init(void);

// Read spindle raw count (4x quadrature)
int32_t spindle_read_count(void);

// Read spindle RPM (averaged over window)
float spindle_read_rpm(void);

// Read spindle direction: 1 = forward, -1 = reverse, 0 = stopped
int8_t spindle_read_direction(void);

// Read X-axis position
axis_position_t x_axis_read(void);

// Read Z-axis position (from scale)
axis_position_t z_axis_read(void);

// Zero an axis count
void spindle_zero(void);
void x_axis_zero(void);
void z_axis_zero(void);

// Set axis to a preset value (in mm)
void x_axis_preset(float value_mm);
void z_axis_preset(float value_mm);

// Called periodically from Core 0 to update RPM calculation
void encoder_update(void);

#endif // ENCODER_H
