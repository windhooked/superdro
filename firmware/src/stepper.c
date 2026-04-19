#include "stepper.h"
#include "pins.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "stepper.pio.h"

#define STEPPER_PIO         pio1
#define STEPPER_FIFO_DEPTH  4
#define SYS_HZ              125000000u

// C-axis VFD pulse width: 5 µs (opto input minimum, layer A §5)
#define C_PULSE_WIDTH_US    5.0f

typedef struct {
    uint     step_pin;
    uint     dir_pin;
    int      enable_pin;    // -1 = no enable
    uint     sm;
    bool     dir_forward;
    int32_t  position;
} axis_hw_t;

static axis_hw_t g_axes[AXIS_COUNT] = {
    [AXIS_Z] = {PIN_Z_STEP, PIN_Z_DIR, PIN_Z_ENABLE, 0, true, 0},
    [AXIS_X] = {PIN_X_STEP, PIN_X_DIR, PIN_X_ENABLE, 1, true, 0},
    [AXIS_C] = {PIN_C_STEP, PIN_C_DIR, -1,            2, true, 0},
};

static uint s_offset = 0;

static void init_axis(axis_hw_t *ax, float pulse_width_us) {
    stepper_program_init(STEPPER_PIO, ax->sm, s_offset, ax->step_pin);

    gpio_init(ax->dir_pin);
    gpio_set_dir(ax->dir_pin, GPIO_OUT);
    gpio_put(ax->dir_pin, true);
    ax->dir_forward = true;
    ax->position    = 0;

    if (ax->enable_pin >= 0) {
        gpio_init((uint)ax->enable_pin);
        gpio_set_dir((uint)ax->enable_pin, GPIO_OUT);
        gpio_put((uint)ax->enable_pin, true);  // HIGH = disabled (active-low)
    }

    // Reload Y register for this axis's pulse width
    uint32_t sys_hz = clock_get_hz(clk_sys);
    uint32_t pulse_cycles = (uint32_t)(pulse_width_us * (float)sys_hz / 1e6f) - 2;

    pio_sm_put_blocking(STEPPER_PIO, ax->sm, pulse_cycles);
    pio_sm_exec(STEPPER_PIO, ax->sm, pio_encode_pull(false, true));
    pio_sm_exec(STEPPER_PIO, ax->sm, pio_encode_mov(pio_y, pio_osr));
}

void stepper_init(void) {
    s_offset = pio_add_program(STEPPER_PIO, &stepper_program);

    float z_pw = 2.5f;
    float x_pw = 2.5f;
    float c_pw = C_PULSE_WIDTH_US;

    init_axis(&g_axes[AXIS_Z], z_pw);
    init_axis(&g_axes[AXIS_X], x_pw);
    init_axis(&g_axes[AXIS_C], c_pw);
}

void __not_in_flash_func(stepper_enable)(stepper_axis_t axis, bool en) {
    axis_hw_t *ax = &g_axes[axis];
    if (ax->enable_pin >= 0) {
        gpio_put((uint)ax->enable_pin, !en);  // active-low
    }
}

void __not_in_flash_func(stepper_set_dir)(stepper_axis_t axis, bool forward) {
    axis_hw_t *ax = &g_axes[axis];
    ax->dir_forward = forward;
    gpio_put(ax->dir_pin, forward);
}

bool __not_in_flash_func(stepper_push)(stepper_axis_t axis, uint32_t delay_cycles) {
    axis_hw_t *ax = &g_axes[axis];
    if (pio_sm_is_tx_fifo_full(STEPPER_PIO, ax->sm)) {
        return false;
    }
    pio_sm_put(STEPPER_PIO, ax->sm, delay_cycles);
    return true;
}

unsigned int __not_in_flash_func(stepper_fifo_free)(stepper_axis_t axis) {
    return STEPPER_FIFO_DEPTH
        - pio_sm_get_tx_fifo_level(STEPPER_PIO, g_axes[axis].sm);
}

void __not_in_flash_func(stepper_stop)(stepper_axis_t axis) {
    uint sm = g_axes[axis].sm;
    pio_sm_set_enabled(STEPPER_PIO, sm, false);
    pio_sm_clear_fifos(STEPPER_PIO, sm);
    pio_sm_restart(STEPPER_PIO, sm);

    // Reinit to reload Y (pulse width constant)
    float pw = (axis == AXIS_C) ? C_PULSE_WIDTH_US : 2.5f;
    uint32_t sys_hz = SYS_HZ;
    uint32_t pulse_cycles = (uint32_t)(pw * (float)sys_hz / 1e6f) - 2;

    stepper_program_init(STEPPER_PIO, sm, s_offset, g_axes[axis].step_pin);
    pio_sm_put_blocking(STEPPER_PIO, sm, pulse_cycles);
    pio_sm_exec(STEPPER_PIO, sm, pio_encode_pull(false, true));
    pio_sm_exec(STEPPER_PIO, sm, pio_encode_mov(pio_y, pio_osr));
}

uint32_t stepper_delay_for_rate(stepper_axis_t axis, uint32_t steps_per_sec) {
    if (steps_per_sec == 0) return UINT32_MAX;
    (void)axis;
    return SYS_HZ / steps_per_sec;
}

// --- Legacy Z-only wrappers ---

void stepper_enable_z(bool en) { stepper_enable(AXIS_Z, en); }
void stepper_set_dir_z(bool f) { stepper_set_dir(AXIS_Z, f); }
bool stepper_get_dir_z(void)   { return g_axes[AXIS_Z].dir_forward; }

bool stepper_push_step(uint32_t delay_cycles) {
    axis_hw_t *ax = &g_axes[AXIS_Z];
    if (!stepper_push(AXIS_Z, delay_cycles)) return false;
    ax->position += ax->dir_forward ? 1 : -1;
    return true;
}

uint32_t stepper_delay_from_rate(float steps_per_sec) {
    if (steps_per_sec <= 0.0f) return UINT32_MAX;
    return (uint32_t)((float)SYS_HZ / steps_per_sec);
}

void stepper_stop_z(void)        { stepper_stop(AXIS_Z); }
int32_t stepper_get_position(void) { return g_axes[AXIS_Z].position; }
void stepper_zero_position(void)   { g_axes[AXIS_Z].position = 0; }
void stepper_set_position(int32_t p) { g_axes[AXIS_Z].position = p; }
unsigned int stepper_fifo_free_z(void) { return stepper_fifo_free(AXIS_Z); }
void stepper_update(void) {}
