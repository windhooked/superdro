#include "encoder.h"
#include "config.h"
#include "pins.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "quadrature.pio.h"

static PIO pio = pio0;
static uint sm_spindle = 0;
static uint sm_x_axis  = 1;
static uint sm_z_axis  = 2;

static volatile int32_t spindle_count;
static volatile int32_t x_count;
static volatile int32_t x_offset;      // for zero/preset
static volatile int32_t z_count;
static volatile int32_t z_offset;      // for zero/preset

// RPM calculation
#define RPM_WINDOW_US 50000  // 50ms
static int32_t  rpm_last_count;
static uint64_t rpm_last_time_us;
static float    rpm_current;
static int8_t   spindle_dir;

// Spindle index pulse ISR
static void spindle_index_isr(uint gpio, uint32_t events) {
    (void)gpio;
    (void)events;
    // Could be used for revolution counting / multi-pass threading alignment
}

void encoder_init(void) {
    // Load the quadrature PIO program
    uint offset = pio_add_program(pio, &quadrature_program);

    // SM 0: spindle encoder on GP2/GP3
    quadrature_program_init(pio, sm_spindle, offset, PIN_SPINDLE_A);

    // SM 1: X-axis scale on GP5/GP6
    quadrature_program_init(pio, sm_x_axis, offset, PIN_X_SCALE_A);

    // SM 2: Z-axis scale on GP20/GP21
    quadrature_program_init(pio, sm_z_axis, offset, PIN_Z_SCALE_A);

    // Spindle index pulse (GP4) — GPIO interrupt
    gpio_init(PIN_SPINDLE_INDEX);
    gpio_set_dir(PIN_SPINDLE_INDEX, GPIO_IN);
    gpio_pull_up(PIN_SPINDLE_INDEX);
    gpio_set_irq_enabled_with_callback(PIN_SPINDLE_INDEX,
        GPIO_IRQ_EDGE_FALL, true, spindle_index_isr);

    rpm_last_time_us = time_us_64();
    rpm_last_count = 0;
    rpm_current = 0.0f;
    spindle_dir = 0;
    x_offset = 0;
    z_offset = 0;
}

void encoder_update(void) {
    // Read raw counts from PIO FIFOs (non-blocking)
    spindle_count = quadrature_get_count(pio, sm_spindle);
    x_count = quadrature_get_count(pio, sm_x_axis);
    z_count = quadrature_get_count(pio, sm_z_axis);

    // RPM calculation over window
    uint64_t now = time_us_64();
    uint64_t dt = now - rpm_last_time_us;
    if (dt >= RPM_WINDOW_US) {
        int32_t current = spindle_count;
        int32_t delta = current - rpm_last_count;
        const machine_config_t *cfg = config_get_all();

        // direction
        if (delta > 0) spindle_dir = 1;
        else if (delta < 0) spindle_dir = -1;
        else spindle_dir = 0;

        // RPM = (delta_counts / counts_per_rev) / (dt_sec) * 60
        float dt_sec = (float)dt / 1000000.0f;
        if (cfg->spindle_counts_per_rev > 0 && dt_sec > 0.0f) {
            float revs = (float)abs(delta) / (float)cfg->spindle_counts_per_rev;
            rpm_current = (revs / dt_sec) * 60.0f;
        } else {
            rpm_current = 0.0f;
        }

        rpm_last_count = current;
        rpm_last_time_us = now;
    }
}

int32_t spindle_read_count(void) {
    return spindle_count;
}

float spindle_read_rpm(void) {
    return rpm_current;
}

int8_t spindle_read_direction(void) {
    return spindle_dir;
}

axis_position_t x_axis_read(void) {
    const machine_config_t *cfg = config_get_all();
    int32_t adjusted = x_count - x_offset;
    float mm = (float)adjusted * cfg->x_scale_resolution_mm;
    return (axis_position_t){
        .raw_count = adjusted,
        .position_mm = mm,
    };
}

void spindle_zero(void) {
    // Reset PIO counter — write 0 to the TX FIFO to reset
    pio_sm_exec(pio, sm_spindle, pio_encode_set(pio_x, 0));
    spindle_count = 0;
    rpm_last_count = 0;
}

void x_axis_zero(void) {
    x_offset = x_count;
}

void x_axis_preset(float value_mm) {
    const machine_config_t *cfg = config_get_all();
    if (cfg->x_scale_resolution_mm > 0.0f) {
        int32_t target_counts = (int32_t)(value_mm / cfg->x_scale_resolution_mm);
        x_offset = x_count - target_counts;
    }
}

axis_position_t z_axis_read(void) {
    const machine_config_t *cfg = config_get_all();
    int32_t adjusted = z_count - z_offset;
    float mm = (float)adjusted * cfg->z_scale_resolution_mm;
    return (axis_position_t){
        .raw_count = adjusted,
        .position_mm = mm,
    };
}

void z_axis_zero(void) {
    z_offset = z_count;
}

void z_axis_preset(float value_mm) {
    const machine_config_t *cfg = config_get_all();
    if (cfg->z_scale_resolution_mm > 0.0f) {
        int32_t target_counts = (int32_t)(value_mm / cfg->z_scale_resolution_mm);
        z_offset = z_count - target_counts;
    }
}
