// Unit tests for protocol.c — JSON serial protocol
// Compile with: gcc -I mocks -I ../src -o test_protocol test_protocol.c ../src/protocol.c ../src/config.c ../src/encoder.c

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "protocol.h"

#define TEST(name) static void name(void)
#define RUN(name) do { printf("  " #name "..."); name(); printf(" OK\n"); } while(0)

// We test the status formatting and JSON helpers indirectly —
// verify they don't crash and produce output on stdout.

TEST(test_status_json_idle) {
    status_snapshot_t s = {
        .x_pos_mm = 12.450f,
        .z_pos_mm = -35.200f,
        .rpm = 820.0f,
        .state = STATE_IDLE,
        .feed_hold = false,
        .estop = false,
    };
    // Just verify it doesn't crash — actual output goes to stdout
    // In CI, we can capture and parse the JSON
    protocol_send_status(&s);
}

TEST(test_status_json_alarm) {
    status_snapshot_t s = {
        .x_pos_mm = 0.0f,
        .z_pos_mm = 0.0f,
        .rpm = 0.0f,
        .state = STATE_ALARM,
        .feed_hold = false,
        .estop = true,
    };
    protocol_send_status(&s);
}

TEST(test_ack_ok) {
    protocol_send_ack("zero", true, NULL);
}

TEST(test_ack_error) {
    protocol_send_ack("zero", false, "unknown axis");
}

TEST(test_state_strings) {
    // Verify all state enum values produce valid JSON (no crash)
    machine_state_t states[] = {
        STATE_IDLE, STATE_JOGGING, STATE_THREADING,
        STATE_CYCLE, STATE_FEED_HOLD, STATE_ALARM
    };
    for (int i = 0; i < 6; i++) {
        status_snapshot_t s = { .state = states[i] };
        protocol_send_status(&s);
    }
}

TEST(test_large_values) {
    status_snapshot_t s = {
        .x_pos_mm = 999.999f,
        .z_pos_mm = -999.999f,
        .rpm = 9999.0f,
        .state = STATE_IDLE,
        .feed_hold = false,
        .estop = false,
    };
    protocol_send_status(&s);
}

TEST(test_zero_values) {
    status_snapshot_t s = {
        .x_pos_mm = 0.0f,
        .z_pos_mm = 0.0f,
        .rpm = 0.0f,
        .state = STATE_IDLE,
        .feed_hold = false,
        .estop = false,
    };
    protocol_send_status(&s);
}

int main(void) {
    // Initialize dependencies
    extern bool config_load(void);
    config_load();

    protocol_init();

    printf("Protocol tests:\n");
    RUN(test_status_json_idle);
    RUN(test_status_json_alarm);
    RUN(test_ack_ok);
    RUN(test_ack_error);
    RUN(test_state_strings);
    RUN(test_large_values);
    RUN(test_zero_values);
    printf("All protocol tests passed!\n\n");
    return 0;
}
