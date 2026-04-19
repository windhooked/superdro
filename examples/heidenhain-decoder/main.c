/**
 * Heidenhain Glass Scale Decoder — Raspberry Pi Pico (RP2040)
 * ============================================================
 * PIO-based x4 quadrature decoding + distance-coded reference mark
 * absolute position computation.
 *
 * Output: UART0 (TX=GP0, RX=GP1) at 115200 baud.
 *
 * Wiring (RS-422 line receiver required — e.g. AM26LS32 / SN75175):
 * ------------------------------------------------------------------
 *   Heidenhain Signal      RS-422 Rx Output    Pico GPIO
 *   Ua1  / Ua1_inv   -->   Ch.A TTL out   -->  GP2  (Quadrature A)
 *   Ua2  / Ua2_inv   -->   Ch.B TTL out   -->  GP3  (Quadrature B)  [must be A+1]
 *   Ua0  / Ua0_inv   -->   Ch.C TTL out   -->  GP4  (Index pulse)
 *   5V / GND as per encoder specs
 *
 *   UART0 TX = GP0  -->  Connect to receiving device RX
 *   UART0 RX = GP1  -->  (optional, for commands)
 *
 * Resolution: signal_period_um / 4 (x4 decode) counts per µm
 *
 * Distance-Coded Reference Marks (models ending in "C"):
 * -------------------------------------------------------
 * The scale has multiple reference marks spaced at varying intervals.
 * After traversing TWO successive reference marks, the absolute position
 * is computed using the Heidenhain formula:
 *
 *   P1 = ((abs(B) - sgn(B) - 1) / 2) * N + ((sgn(B) - sgn(D)) / 2) * abs(MRR)
 *   where B = 2 * MRR - N
 *
 *   P1  = absolute position of first traversed ref mark (in signal periods)
 *   MRR = number of signal periods between the two traversed ref marks
 *   N   = nominal increment (5000 for LF 4µm scales)
 *   D   = direction of traverse (+1 = right, -1 = left)
 *
 * Once P1 is known, the absolute position at any point is:
 *   abs_pos = P1 * signal_period + offset_from_ref_mark
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

#include "quadrature_encoder.pio.h"

// ============================================================================
// Configuration — adjust these for your specific scale model
// ============================================================================

// Pin assignments
#define PIN_QUAD_A      2       // Quadrature channel A (PIO input base)
                                // Channel B is automatically PIN_QUAD_A + 1 (GP3)
#define PIN_INDEX       4       // Index / reference pulse (Ua0)

// UART
#define UART_ID         uart0
#define UART_BAUD       115200
#define UART_TX_PIN     0
#define UART_RX_PIN     1

// PIO
#define PIO_INSTANCE    pio0
#define PIO_SM          0

// Scale parameters — CHANGE THESE FOR YOUR SCALE MODEL
// ┌─────────┬───────────────┬──────────────────┬───────────────┐
// │ Series  │ Signal Period  │ Nominal Incr (N) │ Max traverse  │
// ├─────────┼───────────────┼──────────────────┼───────────────┤
// │ LF (C)  │     4 µm      │      5000        │    20 mm      │
// │ LS (C)  │    20 µm      │      1000        │    20 mm      │
// │ LB (C)  │    40 µm      │      2000        │    80 mm      │
// └─────────┴───────────────┴──────────────────┴───────────────┘
#define SIGNAL_PERIOD_UM    20      // Signal period in micrometers (LS series = 20, LF = 4, LB = 40)
#define NOMINAL_INCREMENT_N 1000    // N: nominal increment in signal periods (LS = 1000, LF = 5000, LB = 2000)
#define COUNTS_PER_PERIOD   4       // x4 decoding

// Output interval
#define OUTPUT_INTERVAL_MS  100

// ============================================================================
// Distance-coded reference mark state machine
// ============================================================================

typedef enum {
    REF_STATE_IDLE,             // No reference marks seen yet
    REF_STATE_FIRST_SEEN,       // First ref mark captured, waiting for second
    REF_STATE_ABSOLUTE_KNOWN    // Absolute position established
} ref_state_t;

typedef struct {
    ref_state_t state;

    // Incremental count at each reference mark (in quadrature counts)
    int32_t first_ref_count;
    int32_t second_ref_count;

    // Direction of traverse when first ref was seen
    int32_t first_ref_direction;    // +1 or -1

    // Computed absolute position of first ref mark (in µm)
    int64_t first_ref_abs_um;

    // Offset: absolute_position = incremental_count + abs_offset
    int64_t abs_offset;

    // Total number of ref marks seen
    uint32_t ref_count;

    // Is absolute position valid?
    bool absolute_valid;
} ref_decoder_t;

// ============================================================================
// Globals
// ============================================================================

static volatile int32_t  last_isr_count = 0;
static volatile int32_t  prev_isr_count = 0;
static volatile bool     new_index_pulse = false;
static ref_decoder_t     ref_decoder;

// ============================================================================
// Forward declarations
// ============================================================================

static void     index_isr_callback(uint gpio, uint32_t events);
static void     uart_send(const char *str);
static int32_t  read_position(void);
static void     ref_decoder_init(ref_decoder_t *rd);
static void     ref_decoder_on_index(ref_decoder_t *rd, int32_t count, int32_t direction);
static int64_t  compute_absolute_um(ref_decoder_t *rd, int32_t incremental_count);
static int32_t  compute_P1(int32_t MRR, int32_t N, int32_t D);

// ============================================================================
// Heidenhain Distance-Coded Reference Mark Formula
// ============================================================================

/**
 * Compute P1: absolute position of the FIRST traversed reference mark
 * in signal periods, using the Heidenhain formula.
 *
 * Formula (from Heidenhain "Linear Encoders for NC Machine Tools"):
 *
 *   B  = 2 * MRR - N
 *   P1 = ((|B| - sgn(B) - 1) / 2) * N  +  ((sgn(B) - sgn(D)) / 2) * |MRR|
 *
 * @param MRR  Number of signal periods between the two traversed ref marks (always > 0)
 * @param N    Nominal increment (5000 for LF 4µm, 1000 for LS 20µm, 2000 for LB 40µm)
 * @param D    Direction of traverse: +1 = rightward (as installed), -1 = leftward
 * @return     P1 in signal periods from scale origin
 */
static int32_t compute_P1(int32_t MRR, int32_t N, int32_t D)
{
    int32_t B = 2 * MRR - N;

    // sgn: returns +1 or -1 (per Heidenhain definition, never 0)
    int32_t sgn_B = (B >= 0) ? 1 : -1;
    int32_t sgn_D = (D >= 0) ? 1 : -1;

    int32_t abs_B   = (B >= 0) ? B : -B;
    int32_t abs_MRR = (MRR >= 0) ? MRR : -MRR;

    // Both divisions yield integers by design of the distance coding
    int32_t term1 = ((abs_B - sgn_B - 1) / 2) * N;
    int32_t term2 = ((sgn_B - sgn_D) / 2) * abs_MRR;

    return term1 + term2;
}

// ============================================================================
// Reference Decoder State Machine
// ============================================================================

static void ref_decoder_init(ref_decoder_t *rd)
{
    rd->state = REF_STATE_IDLE;
    rd->first_ref_count = 0;
    rd->second_ref_count = 0;
    rd->first_ref_direction = 0;
    rd->first_ref_abs_um = 0;
    rd->abs_offset = 0;
    rd->ref_count = 0;
    rd->absolute_valid = false;
}

/**
 * Called each time an index (reference mark) pulse is detected.
 *
 * @param rd        Reference decoder state
 * @param count     Current incremental count (quadrature counts, 1 count = 1 µm)
 * @param direction Traverse direction at time of detection (+1 or -1)
 */
static void ref_decoder_on_index(ref_decoder_t *rd, int32_t count, int32_t direction)
{
    rd->ref_count++;

    switch (rd->state) {

    case REF_STATE_IDLE:
        // First reference mark — record and wait for second
        rd->first_ref_count = count;
        rd->first_ref_direction = direction;
        rd->state = REF_STATE_FIRST_SEEN;
        break;

    case REF_STATE_FIRST_SEEN:
    {
        // Second reference mark — compute absolute position!
        rd->second_ref_count = count;

        // Distance between marks in quadrature counts
        int32_t delta_counts = rd->second_ref_count - rd->first_ref_count;

        // Convert to signal periods (absolute value)
        int32_t MRR = abs(delta_counts) / COUNTS_PER_PERIOD;

        // Direction is from first ref mark detection
        int32_t D = rd->first_ref_direction;

        // Compute P1 (position of first ref mark in signal periods)
        int32_t P1_periods = compute_P1(MRR, NOMINAL_INCREMENT_N, D);

        // Convert P1 to µm
        rd->first_ref_abs_um = (int64_t)P1_periods * SIGNAL_PERIOD_UM;

        // Calculate offset: at first ref mark, absolute = incremental + offset
        rd->abs_offset = rd->first_ref_abs_um - (int64_t)rd->first_ref_count;

        rd->absolute_valid = true;
        rd->state = REF_STATE_ABSOLUTE_KNOWN;

        // Debug output
        char dbg[256];
        snprintf(dbg, sizeof(dbg),
                 "\r\n   [DISTANCE-CODE DECODE]\r\n"
                 "   delta=%+ld counts, MRR=%ld periods, D=%+d\r\n"
                 "   P1=%ld periods => 1st ref @ %+lld um\r\n"
                 "   offset=%+lld (abs = inc + %+lld)\r\n",
                 (long)delta_counts, (long)MRR, (int)D,
                 (long)P1_periods, (long long)rd->first_ref_abs_um,
                 (long long)rd->abs_offset, (long long)rd->abs_offset);
        uart_send(dbg);

        uart_send("   *** ABSOLUTE POSITION ESTABLISHED ***\r\n");
        break;
    }

    case REF_STATE_ABSOLUTE_KNOWN:
    {
        // Additional ref marks: verify consistency
        int64_t abs_at_ref = compute_absolute_um(rd, count);
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
                 " [verify: abs@ref=%+lld um]",
                 (long long)abs_at_ref);
        uart_send(dbg);
        break;
    }
    }
}

/**
 * Compute absolute position in µm from an incremental count.
 * Only valid when ref_decoder.absolute_valid == true.
 */
static int64_t compute_absolute_um(ref_decoder_t *rd, int32_t incremental_count)
{
    return (int64_t)incremental_count + rd->abs_offset;
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    // --- Init UART ---
    uart_init(UART_ID, UART_BAUD);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);

    // --- Init PIO quadrature decoder ---
    pio_add_program(PIO_INSTANCE, &quadrature_encoder_program);
    quadrature_encoder_program_init(PIO_INSTANCE, PIO_SM, PIN_QUAD_A, 0);

    // --- Init reference mark decoder ---
    ref_decoder_init(&ref_decoder);

    // --- Init Index pulse GPIO interrupt ---
    gpio_init(PIN_INDEX);
    gpio_set_dir(PIN_INDEX, GPIO_IN);
    gpio_pull_up(PIN_INDEX);
    gpio_set_irq_enabled_with_callback(PIN_INDEX, GPIO_IRQ_EDGE_RISE, true,
                                       &index_isr_callback);

    // --- Startup banner ---
    uart_send("\r\n================================================\r\n");
    uart_send("Heidenhain Glass Scale Decoder (Pico PIO)\r\n");
    uart_send("Distance-coded reference mark support\r\n");
    char banner[128];
    snprintf(banner, sizeof(banner),
             "Signal period: %d um, N=%d, x4 => 1 um/count\r\n",
             SIGNAL_PERIOD_UM, NOMINAL_INCREMENT_N);
    uart_send(banner);
    uart_send("UART: 115200 8N1 on GP0/GP1\r\n");
    uart_send("Commands: 'z'=zero, 'r'=reset refs, 'a'=query abs\r\n");
    uart_send("Traverse over 2 ref marks to establish absolute pos\r\n");
    uart_send("  (max ~20 mm traverse needed for LS-C series)\r\n");
    uart_send("================================================\r\n\r\n");

    // --- Main loop ---
    int32_t  old_value = 0;
    uint32_t last_output_time = 0;

    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        // --- Process index pulses from ISR ---
        if (new_index_pulse) {
            new_index_pulse = false;

            int32_t count = last_isr_count;
            int32_t prev  = prev_isr_count;
            int32_t direction = (count >= prev) ? 1 : -1;

            ref_decoder_on_index(&ref_decoder, count, direction);

            char buf[160];
            snprintf(buf, sizeof(buf),
                     ">> REF#%lu @ count=%+ld (dir=%+d)",
                     (unsigned long)ref_decoder.ref_count,
                     (long)count, (int)direction);
            uart_send(buf);

            if (ref_decoder.absolute_valid) {
                int64_t abs_um = compute_absolute_um(&ref_decoder, count);
                snprintf(buf, sizeof(buf),
                         " ABS=%+lld um (%+lld.%03lld mm)",
                         (long long)abs_um,
                         (long long)(abs_um / 1000),
                         (long long)(llabs(abs_um) % 1000));
                uart_send(buf);
            } else if (ref_decoder.state == REF_STATE_FIRST_SEEN) {
                uart_send(" [need 1 more ref mark]");
            }
            uart_send("\r\n");
        }

        // --- Handle serial commands ---
        while (uart_is_readable(UART_ID)) {
            char c = uart_getc(UART_ID);
            if (c == 'z' || c == 'Z') {
                pio_sm_set_enabled(PIO_INSTANCE, PIO_SM, false);
                quadrature_encoder_program_init(PIO_INSTANCE, PIO_SM, PIN_QUAD_A, 0);
                old_value = 0;
                ref_decoder_init(&ref_decoder);
                uart_send(">> Zeroed. Traverse 2 refs to re-establish absolute.\r\n");
            } else if (c == 'r' || c == 'R') {
                ref_decoder_init(&ref_decoder);
                uart_send(">> Refs reset. Traverse 2 ref marks.\r\n");
            } else if (c == 'a' || c == 'A') {
                if (ref_decoder.absolute_valid) {
                    int32_t pos = read_position();
                    int64_t abs_um = compute_absolute_um(&ref_decoder, pos);
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                             ">> ABS: %+lld um (%+lld.%03lld mm)\r\n",
                             (long long)abs_um,
                             (long long)(abs_um / 1000),
                             (long long)(llabs(abs_um) % 1000));
                    uart_send(buf);
                } else {
                    uart_send(">> ABS: not established\r\n");
                }
            }
        }

        // --- Periodic position output ---
        if (now - last_output_time >= OUTPUT_INTERVAL_MS) {
            last_output_time = now;

            int32_t pos = read_position();
            int32_t delta = pos - old_value;
            old_value = pos;

            char buf[200];
            int32_t mm_i = pos / 1000;
            int32_t mm_f = abs(pos % 1000);

            snprintf(buf, sizeof(buf),
                     "INC:%+ld.%03ld mm D:%+ld",
                     (long)mm_i, (long)mm_f, (long)delta);
            uart_send(buf);

            if (ref_decoder.absolute_valid) {
                int64_t abs_um = compute_absolute_um(&ref_decoder, pos);
                snprintf(buf, sizeof(buf),
                         " ABS:%+lld.%03lld mm",
                         (long long)(abs_um / 1000),
                         (long long)(llabs(abs_um) % 1000));
                uart_send(buf);
            } else {
                const char *tag = (ref_decoder.state == REF_STATE_FIRST_SEEN)
                                  ? " ABS:--- [1/2 ref]"
                                  : " ABS:--- [no ref]";
                uart_send(tag);
            }

            uart_send("\r\n");
        }

        sleep_us(100);
    }

    return 0;
}

// ============================================================================
// Index Pulse ISR
// ============================================================================

static void index_isr_callback(uint gpio, uint32_t events)
{
    if (gpio == PIN_INDEX) {
        prev_isr_count = last_isr_count;
        last_isr_count = read_position();
        new_index_pulse = true;
    }
}

// ============================================================================
// Helpers
// ============================================================================

static int32_t read_position(void)
{
    return quadrature_encoder_get_count(PIO_INSTANCE, PIO_SM);
}

static void uart_send(const char *str)
{
    uart_puts(UART_ID, str);
}
