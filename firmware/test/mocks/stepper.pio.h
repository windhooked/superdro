#ifndef MOCK_STEPPER_PIO_H
#define MOCK_STEPPER_PIO_H

#include "hardware/pio.h"

// Stub PIO program (replaces auto-generated stepper.pio.h)
static const uint16_t stepper_program_instructions[] = { 0 };

static const pio_program_t stepper_program = {
    .instructions = stepper_program_instructions,
    .length = 1,
    .origin = -1,
};

static inline pio_sm_config stepper_program_get_default_config(uint offset) {
    (void)offset;
    return (pio_sm_config){0};
}

#define STEPPER_PULSE_WIDTH_US  2.5f
#define STEPPER_OVERHEAD_CYCLES 5

static inline void stepper_program_init(PIO pio, uint sm, uint offset, uint step_pin) {
    (void)pio; (void)sm; (void)offset; (void)step_pin;
}

// Working delay calculation using 133 MHz constant (matches real firmware)
static inline uint32_t stepper_calc_delay(float steps_per_sec) {
    uint32_t sys_hz = 133000000;
    uint32_t pulse_cycles = (uint32_t)(STEPPER_PULSE_WIDTH_US * (float)sys_hz / 1e6f);
    uint32_t total_cycles = (uint32_t)((float)sys_hz / steps_per_sec);

    if (total_cycles <= (pulse_cycles + STEPPER_OVERHEAD_CYCLES)) {
        return 0;
    }
    return total_cycles - pulse_cycles - STEPPER_OVERHEAD_CYCLES;
}

#endif // MOCK_STEPPER_PIO_H
