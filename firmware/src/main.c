#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/cyw43_arch.h"
#include "config.h"
#include "encoder.h"
#include "spindle.h"
#include "stepper.h"
#include "els.h"
#include "protocol.h"
#include "safety.h"

// Core 1 entry: USB protocol + housekeeping (~50 Hz)
static void core1_main(void) {
    protocol_init();

    while (true) {
        protocol_process_rx();

        const els_status_t *st = els_status_read();
        if (st) {
            // Map ELS status → legacy status_snapshot_t for protocol_send_status
            status_snapshot_t snap = {
                .rpm       = st->spindle_rpm,
                .estop     = st->estop,
                .els_state = (uint8_t)st->state,
                .els_error = st->z_backlog,
            };

            // Derive mm positions from step counts + config
            const machine_config_t *cfg = config_get_all();
            if (cfg->z_steps_per_mm > 0.0f)
                snap.z_pos_mm = (float)st->z_pos_steps / cfg->z_steps_per_mm;
            if (cfg->x_steps_per_mm > 0.0f)
                snap.x_pos_mm = (float)st->x_pos_steps / cfg->x_steps_per_mm;

            snap.state = (st->state == ELS_STATE_THREADING_ENGAGED) ? STATE_THREADING :
                         (st->state == ELS_STATE_THREADING_HOLD)    ? STATE_FEED_HOLD  :
                         (st->state == ELS_STATE_JOG)               ? STATE_JOGGING    :
                         (st->state == ELS_STATE_FAULT)             ? STATE_ALARM      :
                                                                       STATE_IDLE;
            snap.feed_hold = (st->state == ELS_STATE_THREADING_HOLD);

            protocol_send_status(&snap);
        }

        safety_led_update();
        sleep_ms(20); // ~50 Hz
    }
}

int main(void) {
    stdio_init_all();

    if (cyw43_arch_init()) {
        // CYW43 init failed — continue without LED
    }

    config_load();
    config_recalculate();

    encoder_init();   // X/Z glass-scale decoders (PIO0 SM1/SM2)
    spindle_init();   // Spindle decoder + DMA ring (PIO0 SM0)
    stepper_init();   // All three step generators (PIO1 SM0/1/2)
    els_init();       // FSM + engine + ramp
    safety_init();

    multicore_launch_core1(core1_main);

    // Core 0: hard real-time loop (free-running, no sleep)
    while (true) {
        safety_watchdog_feed();
        safety_debounce_update();

        // ELS FSM + spindle update (spindle_update called inside els_fsm_step)
        els_update();

        // E-stop interlock: FSM handles it internally; still gate engage button
        // Physical button edge-detect → inject FSM events
        {
            static bool prev_engage = false;
            bool engage_now = button_engage_pressed();
            if (engage_now && !prev_engage) {
                els_state_t st = els_get_state();
                if (st == ELS_STATE_IDLE && !safety_estop_active())
                    els_engage();
                else if (st == ELS_STATE_THREADING_ENGAGED ||
                         st == ELS_STATE_FEED_ENGAGED      ||
                         st == ELS_STATE_TAPER_ENGAGED)
                    els_disengage();
            }
            prev_engage = engage_now;
        }
        {
            static bool prev_fh = false;
            bool fh_now = button_feed_hold_pressed();
            if (fh_now && !prev_fh) {
                els_state_t st = els_get_state();
                if (st == ELS_STATE_THREADING_ENGAGED) els_feed_hold();
                else if (st == ELS_STATE_THREADING_HOLD) els_resume();
            }
            prev_fh = fh_now;
        }

        // Update glass-scale DRO (X/Z encoders)
        encoder_update();
    }
}
