#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/cyw43_arch.h"
#include "config.h"
#include "encoder.h"
#include "stepper.h"
#include "els.h"
#include "protocol.h"
#include "safety.h"

// Shared status between cores
static volatile status_snapshot_t g_status;

// Core 1 entry: USB comms + housekeeping
static void core1_main(void) {
    protocol_init();

    while (true) {
        protocol_process_rx();
        protocol_send_status((const status_snapshot_t *)&g_status);
        safety_led_update();
        sleep_ms(20); // ~50 Hz
    }
}

int main(void) {
    stdio_init_all();

    // Initialize CYW43 for LED access (no WiFi/BT networking)
    if (cyw43_arch_init()) {
        // CYW43 init failed — continue without LED
    }

    config_load();
    config_recalculate();
    encoder_init();
    stepper_init();
    els_init();
    safety_init();

    // Launch Core 1
    multicore_launch_core1(core1_main);

    // Core 0: real-time control loop
    while (true) {
        safety_watchdog_feed();
        safety_debounce_update();
        encoder_update();

        // ELS safety interlock: disengage on E-stop
        if (safety_estop_active() && els_get_state() != ELS_IDLE)
            els_disengage();

        // ELS sync loop
        els_update();

        // ELS error threshold → disengage
        if (els_get_state() == ELS_ENGAGED &&
            (els_get_error() > 50 || els_get_error() < -50))
            els_disengage();

        // Engage button edge-detect (toggle: engage/disengage)
        {
            static bool prev_engage = false;
            bool engage_now = button_engage_pressed();
            if (engage_now && !prev_engage) {
                if (els_get_state() == ELS_IDLE && !safety_estop_active())
                    els_engage();
                else if (els_get_state() != ELS_IDLE)
                    els_disengage();
            }
            prev_engage = engage_now;
        }

        // Feed hold button edge-detect (toggle: hold/resume)
        {
            static bool prev_fh = false;
            bool fh_now = button_feed_hold_pressed();
            if (fh_now && !prev_fh) {
                if (els_get_state() == ELS_ENGAGED)
                    els_feed_hold();
                else if (els_get_state() == ELS_FEED_HOLD)
                    els_resume();
            }
            prev_fh = fh_now;
        }

        stepper_update();

        // Update status snapshot
        axis_position_t x = x_axis_read();
        axis_position_t z = z_axis_read();
        float rpm = spindle_read_rpm();

        g_status.x_pos_mm = x.position_mm;
        g_status.z_pos_mm = z.position_mm;
        g_status.rpm = rpm;
        g_status.estop = safety_estop_active();
        g_status.pitch_mm = els_get_pitch();
        g_status.els_state = (uint8_t)els_get_state();
        g_status.els_error = els_get_error();

        // State mapping
        if (safety_alarm_active()) {
            g_status.state = STATE_ALARM;
        } else if (els_get_state() == ELS_ENGAGED) {
            g_status.state = STATE_THREADING;
        } else if (els_get_state() == ELS_FEED_HOLD) {
            g_status.state = STATE_FEED_HOLD;
            g_status.feed_hold = true;
        } else {
            g_status.state = STATE_IDLE;
            g_status.feed_hold = false;
        }

        sleep_us(20); // ~50 kHz loop
    }
}
