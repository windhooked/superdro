#ifndef MOCK_QUADRATURE_PIO_H
#define MOCK_QUADRATURE_PIO_H

// Mock of the auto-generated PIO header
// In real builds, pico_generate_pio_header() creates this from quadrature.pio

#include "hardware/pio.h"

static const uint16_t quadrature_program_instructions[] = { 0 };

static const pio_program_t quadrature_program = {
    .instructions = quadrature_program_instructions,
    .length = 1,
    .origin = -1,
};

static inline pio_sm_config quadrature_program_get_default_config(uint offset) {
    (void)offset;
    return (pio_sm_config){0};
}

static inline void quadrature_program_init(PIO pio, uint sm, uint offset, uint base_pin) {
    (void)pio; (void)sm; (void)offset; (void)base_pin;
}

// Quadrature decode state (defined in mock_flash.c)
extern int32_t s_counts[4];
extern uint8_t s_prev_state[4];

// Indexed by binary pin value: 0b00=0, 0b01=1, 0b10=2, 0b11=3
// Forward (CW):  0→1→3→2, Reverse (CCW): 0→2→3→1
static const int8_t quad_lut[4][4] = {
    //          new: 00  01  10  11
    /* 00 */       { 0, +1, -1,  0},
    /* 01 */       {-1,  0,  0, +1},
    /* 10 */       {+1,  0,  0, -1},
    /* 11 */       { 0, -1, +1,  0},
};

static inline int32_t quadrature_get_count(PIO pio, uint sm) {
    (void)pio;
    while (!pio_sm_is_rx_fifo_empty(pio, sm)) {
        uint32_t raw = pio_sm_get(pio, sm);
        uint8_t new_state = raw & 0x03;
        s_counts[sm] += quad_lut[s_prev_state[sm]][new_state];
        s_prev_state[sm] = new_state;
    }
    return s_counts[sm];
}

#endif
