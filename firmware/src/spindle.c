#include "spindle.h"
#include "pins.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "quadrature.pio.h"

#define SPINDLE_RING_SIZE   256u
#define SYS_HZ              125000000u
#define INDEX_FAULT_PCT     10u
#define RATE_EMA_SHIFT      3u          // alpha = 1/8

// DMA ring — must be aligned to its own byte size for ring-wrap to work
static uint32_t g_ring[SPINDLE_RING_SIZE] __attribute__((aligned(SPINDLE_RING_SIZE * 4)));
static uint32_t g_rhead = 0;
static int      g_dma_ch = -1;
static uint     g_pio_offset = 0;

// Spindle state
static volatile int32_t  g_count     = 0;
static volatile int8_t   g_direction = 0;
static volatile uint32_t g_rate_eps  = 0;   // edges/sec EMA

// Index tracking
static uint8_t  g_prev_i           = 0;
static bool     g_index_latched    = false;
static bool     g_index_fault_flag = false;
static uint32_t g_index_interval_us    = 0;
static uint32_t g_index_interval_ema   = 0;
static uint32_t g_last_index_time_us   = 0;

// Multi-start sync
static bool     g_waiting_index  = false;
static bool     g_waiting_offset = false;
static uint32_t g_offset_remain  = 0;

// Rate measurement
static uint32_t g_last_update_us = 0;
static uint32_t g_edges_since_last = 0;

void spindle_init(void) {
    const machine_config_t *cfg = config_get_all();

    g_pio_offset = pio_add_program(pio0, &quadrature_3pin_program);
    quadrature_3pin_program_init(pio0, 0, g_pio_offset, PIN_SPINDLE_A);

    g_dma_ch = dma_claim_unused_channel(true);

    dma_channel_config dc = dma_channel_get_default_config(g_dma_ch);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_ring(&dc, true, 10);  // wrap write addr at 2^10 = 1024 bytes
    channel_config_set_dreq(&dc, DREQ_PIO0_RX0);

    dma_channel_configure(
        g_dma_ch, &dc,
        g_ring,
        &pio0_hw->rxf[0],
        0xFFFFFFFFu,  // continuous
        true
    );

    g_last_update_us = time_us_32();
    (void)cfg;
}

static inline uint32_t __not_in_flash_func(ring_write_idx)(void) {
    uint32_t waddr = dma_hw->ch[g_dma_ch].write_addr;
    return (waddr - (uint32_t)g_ring) / sizeof(uint32_t);
}

int32_t __not_in_flash_func(spindle_update)(void) {
    uint32_t widx  = ring_write_idx() & (SPINDLE_RING_SIZE - 1);
    uint32_t now   = time_us_32();
    int32_t  delta = 0;

    // 8x8 LUT for 4x quadrature decode on bits [1:0]
    static const int8_t lut[4][4] = {
        { 0, +1, -1,  0},
        {-1,  0,  0, +1},
        {+1,  0,  0, -1},
        { 0, -1, +1,  0},
    };

    static uint8_t prev_ab = 0;

    while (g_rhead != widx) {
        uint32_t word  = g_ring[g_rhead];
        g_rhead = (g_rhead + 1) & (SPINDLE_RING_SIZE - 1);

        uint8_t new3   = (uint8_t)(word & 0x07);
        uint8_t new_ab = new3 & 0x03;
        uint8_t new_i  = (new3 >> 2) & 0x01;

        // Quadrature decode
        int8_t d = lut[prev_ab][new_ab];
        if (d != 0) {
            g_count += d;
            delta   += d;
            g_edges_since_last++;

            // Count offset edges for multi-start sync
            if (g_waiting_offset && g_offset_remain > 0) {
                g_offset_remain--;
                if (g_offset_remain == 0) {
                    g_waiting_offset = false;
                    g_index_latched  = true;
                }
            }
        }

        // Index pulse detection (rising edge of I)
        if (new_i && !g_prev_i) {
            uint32_t interval = now - g_last_index_time_us;
            g_last_index_time_us = now;

            // Index interval fault detection
            if (g_index_interval_ema > 0) {
                uint32_t dev = (interval > g_index_interval_ema)
                    ? interval - g_index_interval_ema
                    : g_index_interval_ema - interval;
                if (dev * 100u / g_index_interval_ema > INDEX_FAULT_PCT) {
                    g_index_fault_flag = true;
                }
            }
            g_index_interval_ema = g_index_interval_ema
                - (g_index_interval_ema >> RATE_EMA_SHIFT)
                + (interval >> RATE_EMA_SHIFT);

            if (g_waiting_index) {
                g_waiting_index = false;
                if (g_offset_remain == 0) {
                    g_index_latched = true;
                } else {
                    g_waiting_offset = true;
                }
            }
        }

        g_prev_i = new_i;
        prev_ab  = new_ab;
    }

    // Update direction
    if (delta > 0)       g_direction = +1;
    else if (delta < 0)  g_direction = -1;
    else if (g_edges_since_last == 0) g_direction = 0;

    // Update rate EMA once per call
    uint32_t dt_us = now - g_last_update_us;
    if (dt_us > 0 && g_edges_since_last > 0) {
        uint32_t current_eps = (uint32_t)((uint64_t)g_edges_since_last * 1000000u / dt_us);
        g_rate_eps = g_rate_eps
            - (g_rate_eps >> RATE_EMA_SHIFT)
            + (current_eps >> RATE_EMA_SHIFT);
        g_edges_since_last = 0;
        g_last_update_us   = now;
    } else if (dt_us > 500000u) {
        // Spindle stopped: decay to zero
        g_rate_eps         = g_rate_eps >> 1;
        g_last_update_us   = now;
        g_edges_since_last = 0;
        g_direction        = 0;
    }

    return delta;
}

int32_t __not_in_flash_func(spindle_read_count)(void) {
    return g_count;
}

uint32_t __not_in_flash_func(spindle_read_rate_eps)(void) {
    return g_rate_eps;
}

int8_t __not_in_flash_func(spindle_direction)(void) {
    return g_direction;
}

bool __not_in_flash_func(spindle_index_latched)(void) {
    return g_index_latched;
}

void __not_in_flash_func(spindle_index_latch_clear)(void) {
    g_index_latched    = false;
    g_index_fault_flag = false;
}

bool __not_in_flash_func(spindle_index_fault)(void) {
    return g_index_fault_flag;
}

void __not_in_flash_func(spindle_arm_start_offset)(uint32_t pulses) {
    g_waiting_index  = true;
    g_waiting_offset = false;
    g_offset_remain  = pulses;
    g_index_latched  = false;
    g_index_fault_flag = false;
}

float spindle_read_rpm(void) {
    if (g_rate_eps == 0) return 0.0f;
    const machine_config_t *cfg = config_get_all();
    return (float)g_rate_eps * 60.0f / (float)cfg->spindle_counts_per_rev;
}
