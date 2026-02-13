#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/cyw43_arch.h"
#include "config.h"
#include "encoder.h"
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
    safety_init();

    // Launch Core 1
    multicore_launch_core1(core1_main);

    // Core 0: real-time control loop
    while (true) {
        safety_watchdog_feed();
        safety_debounce_update();
        encoder_update();

        axis_position_t x = x_axis_read();
        axis_position_t z = z_axis_read();
        float rpm = spindle_read_rpm();

        g_status.x_pos_mm = x.position_mm;
        g_status.z_pos_mm = z.position_mm;
        g_status.rpm = rpm;
        g_status.estop = safety_estop_active();
        g_status.state = safety_alarm_active() ? STATE_ALARM : STATE_IDLE;

        sleep_us(20); // ~50 kHz loop for Phase 1 (DRO only)
    }
}
