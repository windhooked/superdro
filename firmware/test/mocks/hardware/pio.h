#ifndef MOCK_HARDWARE_PIO_H
#define MOCK_HARDWARE_PIO_H

#include <stdint.h>
#include <stdbool.h>

typedef struct pio_hw {} *PIO;
__attribute__((unused)) static struct pio_hw _pio0_hw, _pio1_hw;
#define pio0 (&_pio0_hw)
#define pio1 (&_pio1_hw)

// PIO program structure
typedef struct {
    const uint16_t *instructions;
    uint8_t length;
    int8_t origin;
} pio_program_t;

static inline uint pio_add_program(PIO pio, const pio_program_t *program) {
    (void)pio; (void)program; return 0;
}

static inline void pio_sm_set_consecutive_pindirs(PIO pio, uint sm, uint pin_base, uint pin_count, bool is_out) {
    (void)pio; (void)sm; (void)pin_base; (void)pin_count; (void)is_out;
}

typedef struct {
    uint32_t dummy;
} pio_sm_config;

static inline pio_sm_config pio_get_default_sm_config(void) { return (pio_sm_config){0}; }
static inline void sm_config_set_in_pins(pio_sm_config *c, uint pin) { (void)c; (void)pin; }
static inline void sm_config_set_in_shift(pio_sm_config *c, bool right, bool autopush, uint bits) {
    (void)c; (void)right; (void)autopush; (void)bits;
}
static inline void sm_config_set_clkdiv(pio_sm_config *c, float div) { (void)c; (void)div; }

static inline void pio_sm_init(PIO pio, uint sm, uint offset, const pio_sm_config *config) {
    (void)pio; (void)sm; (void)offset; (void)config;
}
static inline void pio_sm_set_enabled(PIO pio, uint sm, bool enabled) {
    (void)pio; (void)sm; (void)enabled;
}

// Mock FIFO (defined in mock_flash.c)
extern uint32_t _mock_fifo_data[4][16];
extern int _mock_fifo_count[4];
extern int _mock_fifo_read_pos[4];

static inline bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm) {
    (void)pio;
    return _mock_fifo_read_pos[sm] >= _mock_fifo_count[sm];
}

static inline uint32_t pio_sm_get(PIO pio, uint sm) {
    (void)pio;
    if (_mock_fifo_read_pos[sm] < _mock_fifo_count[sm]) {
        return _mock_fifo_data[sm][_mock_fifo_read_pos[sm]++];
    }
    return 0;
}

static inline void mock_fifo_push(uint sm, uint32_t data) {
    if (_mock_fifo_count[sm] < 16) {
        _mock_fifo_data[sm][_mock_fifo_count[sm]++] = data;
    }
}

static inline void mock_fifo_clear(uint sm) {
    _mock_fifo_count[sm] = 0;
    _mock_fifo_read_pos[sm] = 0;
}

// PIO encode helpers
enum pio_src_dest { pio_x = 0, pio_y = 1 };
static inline uint32_t pio_encode_set(enum pio_src_dest dest, uint value) {
    (void)dest; (void)value; return 0;
}
static inline void pio_sm_exec(PIO pio, uint sm, uint32_t instr) {
    (void)pio; (void)sm; (void)instr;
}

#endif
