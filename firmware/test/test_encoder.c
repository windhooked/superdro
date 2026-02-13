// Unit tests for encoder.c — encoder abstraction and RPM calculation
// Compile with: gcc -I mocks -I ../src -o test_encoder test_encoder.c ../src/encoder.c ../src/config.c

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "encoder.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "quadrature.pio.h"
#include "hardware/flash.h"

#define ASSERT_FLOAT_EQ(a, b, eps) assert(fabs((a) - (b)) < (eps))

#define TEST(name) static void name(void)
#define RUN(name) do { printf("  " #name "..."); name(); printf(" OK\n"); } while(0)

// Reset all mock state
static void reset_mocks(void) {
    _mock_time_us = 0;
    memset(_mock_flash, 0xFF, sizeof(_mock_flash));
    for (int i = 0; i < 4; i++) {
        mock_fifo_clear(i);
        s_counts[i] = 0;
        s_prev_state[i] = 0;
    }
}

TEST(test_init_no_crash) {
    reset_mocks();
    config_load();
    encoder_init();
}

TEST(test_spindle_zero_at_start) {
    reset_mocks();
    config_load();
    encoder_init();
    encoder_update();
    assert(spindle_read_count() == 0);
    ASSERT_FLOAT_EQ(spindle_read_rpm(), 0.0f, 0.01f);
}

TEST(test_spindle_count_forward) {
    reset_mocks();
    config_load();
    encoder_init();

    // Simulate quadrature transitions: 00 → 01 → 11 → 10 (forward)
    mock_fifo_push(0, 0x01); // 00 → 01: +1
    mock_fifo_push(0, 0x03); // 01 → 11: +1
    mock_fifo_push(0, 0x02); // 11 → 10: +1
    mock_fifo_push(0, 0x00); // 10 → 00: +1

    encoder_update();
    assert(spindle_read_count() == 4);
}

TEST(test_spindle_count_reverse) {
    reset_mocks();
    config_load();
    encoder_init();

    // Reverse: 00 → 10 → 11 → 01 → 00
    mock_fifo_push(0, 0x02); // 00 → 10: -1
    mock_fifo_push(0, 0x03); // 10 → 11: -1
    mock_fifo_push(0, 0x01); // 11 → 01: -1
    mock_fifo_push(0, 0x00); // 01 → 00: -1

    encoder_update();
    assert(spindle_read_count() == -4);
}

TEST(test_rpm_calculation) {
    reset_mocks();
    config_load();
    encoder_init();

    // At t=0, count = 0
    mock_set_time_us(0);
    encoder_update();

    // Simulate 4000 counts in 50ms = 1 revolution in 50ms = 1200 RPM
    // Push enough transitions to accumulate 4000 counts
    // (We can't push 4000 FIFO entries, so we set the count directly)
    s_counts[0] = 4000;
    mock_set_time_us(50000); // 50ms later
    encoder_update();

    float rpm = spindle_read_rpm();
    ASSERT_FLOAT_EQ(rpm, 1200.0f, 10.0f);
}

TEST(test_rpm_high_speed) {
    reset_mocks();
    config_load();
    encoder_init();

    mock_set_time_us(0);
    encoder_update();

    // 3500 RPM = 3500/60 = 58.33 rev/s
    // In 50ms: 58.33 * 0.05 = 2.917 revolutions = 11,667 counts
    s_counts[0] = 11667;
    mock_set_time_us(50000);
    encoder_update();

    float rpm = spindle_read_rpm();
    ASSERT_FLOAT_EQ(rpm, 3500.0f, 50.0f);
}

TEST(test_spindle_direction) {
    reset_mocks();
    config_load();
    encoder_init();

    mock_set_time_us(0);
    encoder_update();

    // Forward
    s_counts[0] = 100;
    mock_set_time_us(50000);
    encoder_update();
    assert(spindle_read_direction() == 1);

    // Reverse
    s_counts[0] = -50;
    mock_set_time_us(100000);
    encoder_update();
    assert(spindle_read_direction() == -1);
}

TEST(test_x_axis_position) {
    reset_mocks();
    config_load();
    encoder_init();

    // X scale resolution = 0.005mm per count
    // 1000 counts = 5.0mm
    s_counts[1] = 1000;
    encoder_update();

    axis_position_t pos = x_axis_read();
    assert(pos.raw_count == 1000);
    ASSERT_FLOAT_EQ(pos.position_mm, 5.0f, 0.001f);
}

TEST(test_x_axis_zero) {
    reset_mocks();
    config_load();
    encoder_init();

    s_counts[1] = 2000;
    encoder_update();

    x_axis_zero();

    axis_position_t pos = x_axis_read();
    ASSERT_FLOAT_EQ(pos.position_mm, 0.0f, 0.001f);

    // Move further — should be relative to zero point
    s_counts[1] = 2500;
    encoder_update();
    pos = x_axis_read();
    ASSERT_FLOAT_EQ(pos.position_mm, 2.5f, 0.001f); // 500 * 0.005
}

TEST(test_x_axis_preset) {
    reset_mocks();
    config_load();
    encoder_init();

    s_counts[1] = 1000;
    encoder_update();

    x_axis_preset(10.0f); // Set current position to 10.0mm

    axis_position_t pos = x_axis_read();
    ASSERT_FLOAT_EQ(pos.position_mm, 10.0f, 0.001f);
}

TEST(test_x_axis_negative) {
    reset_mocks();
    config_load();
    encoder_init();

    s_counts[1] = -500;
    encoder_update();

    axis_position_t pos = x_axis_read();
    ASSERT_FLOAT_EQ(pos.position_mm, -2.5f, 0.001f);
}

TEST(test_z_axis_position) {
    reset_mocks();
    config_load();
    encoder_init();

    // Z scale resolution = 0.005mm per count (same default as X)
    // 2000 counts = 10.0mm
    s_counts[2] = 2000;
    encoder_update();

    axis_position_t pos = z_axis_read();
    assert(pos.raw_count == 2000);
    ASSERT_FLOAT_EQ(pos.position_mm, 10.0f, 0.001f);
}

TEST(test_z_axis_zero) {
    reset_mocks();
    config_load();
    encoder_init();

    s_counts[2] = 3000;
    encoder_update();

    z_axis_zero();

    axis_position_t pos = z_axis_read();
    ASSERT_FLOAT_EQ(pos.position_mm, 0.0f, 0.001f);

    // Move further — should be relative to zero point
    s_counts[2] = 3200;
    encoder_update();
    pos = z_axis_read();
    ASSERT_FLOAT_EQ(pos.position_mm, 1.0f, 0.001f); // 200 * 0.005
}

TEST(test_z_axis_preset) {
    reset_mocks();
    config_load();
    encoder_init();

    s_counts[2] = 1000;
    encoder_update();

    z_axis_preset(-50.0f); // Set current position to -50.0mm

    axis_position_t pos = z_axis_read();
    ASSERT_FLOAT_EQ(pos.position_mm, -50.0f, 0.001f);
}

TEST(test_z_axis_negative) {
    reset_mocks();
    config_load();
    encoder_init();

    s_counts[2] = -1000;
    encoder_update();

    axis_position_t pos = z_axis_read();
    ASSERT_FLOAT_EQ(pos.position_mm, -5.0f, 0.001f);
}

int main(void) {
    printf("Encoder tests:\n");
    RUN(test_init_no_crash);
    RUN(test_spindle_zero_at_start);
    RUN(test_spindle_count_forward);
    RUN(test_spindle_count_reverse);
    RUN(test_rpm_calculation);
    RUN(test_rpm_high_speed);
    RUN(test_spindle_direction);
    RUN(test_x_axis_position);
    RUN(test_x_axis_zero);
    RUN(test_x_axis_preset);
    RUN(test_x_axis_negative);
    RUN(test_z_axis_position);
    RUN(test_z_axis_zero);
    RUN(test_z_axis_preset);
    RUN(test_z_axis_negative);
    printf("All encoder tests passed!\n\n");
    return 0;
}
