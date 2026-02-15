#include "stepper.h"
#include "pins.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "stepper.pio.h"

#define STEPPER_PIO     pio1
#define STEPPER_SM      0
#define STEPPER_FIFO_DEPTH 4

static uint s_offset = 0;
static volatile int32_t s_position = 0;
static bool s_dir_forward = true;

void stepper_init(void) {
    // Load PIO program and initialize state machine
    s_offset = pio_add_program(STEPPER_PIO, &stepper_program);
    stepper_program_init(STEPPER_PIO, STEPPER_SM, s_offset, PIN_Z_STEP);

    // Direction pin: GP9, output, default forward
    gpio_init(PIN_Z_DIR);
    gpio_set_dir(PIN_Z_DIR, GPIO_OUT);
    gpio_put(PIN_Z_DIR, true);
    s_dir_forward = true;

    // Enable pin: GP10, output, active LOW for CL57T.
    // Start disabled (HIGH) for safety — must be explicitly enabled.
    gpio_init(PIN_Z_ENABLE);
    gpio_set_dir(PIN_Z_ENABLE, GPIO_OUT);
    gpio_put(PIN_Z_ENABLE, true);  // HIGH = disabled

    s_position = 0;
}

void stepper_enable(bool enabled) {
    // CL57T: active low — LOW = enabled, HIGH = disabled
    gpio_put(PIN_Z_ENABLE, !enabled);
}

void stepper_set_dir(bool forward) {
    s_dir_forward = forward;
    gpio_put(PIN_Z_DIR, forward);
}

bool stepper_get_dir(void) {
    return s_dir_forward;
}

bool stepper_push_step(uint32_t delay_cycles) {
    if (pio_sm_is_tx_fifo_full(STEPPER_PIO, STEPPER_SM)) {
        return false;
    }

    pio_sm_put(STEPPER_PIO, STEPPER_SM, delay_cycles);

    if (s_dir_forward) {
        s_position++;
    } else {
        s_position--;
    }

    return true;
}

uint32_t stepper_delay_from_rate(float steps_per_sec) {
    if (steps_per_sec <= 0.0f) {
        return UINT32_MAX;
    }
    return stepper_calc_delay(steps_per_sec);
}

void stepper_stop(void) {
    pio_sm_set_enabled(STEPPER_PIO, STEPPER_SM, false);
    pio_sm_clear_fifos(STEPPER_PIO, STEPPER_SM);
    pio_sm_restart(STEPPER_PIO, STEPPER_SM);

    // Re-initialize SM (reloads Y register for pulse width)
    stepper_program_init(STEPPER_PIO, STEPPER_SM, s_offset, PIN_Z_STEP);
}

int32_t stepper_get_position(void) {
    return s_position;
}

void stepper_zero_position(void) {
    s_position = 0;
}

void stepper_set_position(int32_t pos) {
    s_position = pos;
}

unsigned int stepper_fifo_free(void) {
    return STEPPER_FIFO_DEPTH - pio_sm_get_tx_fifo_level(STEPPER_PIO, STEPPER_SM);
}

void stepper_update(void) {
    // Reserved for future:
    // - CL57T alarm input monitoring
    // - Stall detection
}
