// Unit tests for config.c — machine configuration system
// Compile with: gcc -I mocks -I ../src -o test_config test_config.c ../src/config.c

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "config.h"
#include "hardware/flash.h"

#define ASSERT_FLOAT_EQ(a, b, eps) \
    assert(fabs((a) - (b)) < (eps))

#define TEST(name) static void name(void)
#define RUN(name) do { printf("  " #name "..."); name(); printf(" OK\n"); } while(0)

// Reset mock flash to uninitialized (0xFF) so config_load falls back to defaults
static void reset_flash(void) {
    memset(_mock_flash, 0xFF, sizeof(_mock_flash));
}

// --- Tests ---

TEST(test_defaults_loaded) {
    config_load(); // No valid flash data → defaults
    const machine_config_t *cfg = config_get_all();
    assert(cfg->spindle_ppr == 1000);
    assert(cfg->spindle_quadrature == 4);
    assert(cfg->spindle_counts_per_rev == 4000);
    assert(cfg->spindle_max_rpm == 3500);
    ASSERT_FLOAT_EQ(cfg->z_leadscrew_pitch_mm, 6.0f, 0.001f);
    assert(cfg->z_steps_per_rev == 1000);
    ASSERT_FLOAT_EQ(cfg->z_belt_ratio, 1.0f, 0.001f);
    ASSERT_FLOAT_EQ(cfg->z_scale_resolution_mm, 0.005f, 0.0001f);
    ASSERT_FLOAT_EQ(cfg->x_scale_resolution_mm, 0.005f, 0.0001f);
    assert(cfg->x_is_diameter == true);
    assert(cfg->thread_retract_mode == 0);
    ASSERT_FLOAT_EQ(cfg->thread_compound_angle, 29.5f, 0.01f);
}

TEST(test_derived_values) {
    config_load();
    config_recalculate();
    const machine_config_t *cfg = config_get_all();
    // z_steps_per_mm = 1000 * 1.0 / 6.0 = 166.667
    ASSERT_FLOAT_EQ(cfg->z_steps_per_mm, 166.667f, 0.1f);
    // x_steps_per_mm = 1000 * 1.0 / 3.0 = 333.333
    ASSERT_FLOAT_EQ(cfg->x_steps_per_mm, 333.333f, 0.1f);
    // spindle_counts_per_rev = 1000 * 4 = 4000
    assert(cfg->spindle_counts_per_rev == 4000);
}

TEST(test_kv_get_uint16) {
    config_load();
    char val[32];
    assert(config_get("spindle_ppr", val, sizeof(val)));
    assert(strcmp(val, "1000") == 0);
}

TEST(test_kv_get_float) {
    config_load();
    char val[32];
    assert(config_get("z_leadscrew_pitch_mm", val, sizeof(val)));
    // Should be "6.0000"
    float f = strtof(val, NULL);
    ASSERT_FLOAT_EQ(f, 6.0f, 0.001f);
}

TEST(test_kv_get_bool) {
    config_load();
    char val[32];
    assert(config_get("x_is_diameter", val, sizeof(val)));
    assert(strcmp(val, "true") == 0);
}

TEST(test_kv_get_unknown_key) {
    config_load();
    char val[32];
    assert(!config_get("nonexistent_key", val, sizeof(val)));
}

TEST(test_kv_set_uint16) {
    config_load();
    assert(config_set("spindle_ppr", "2000"));
    const machine_config_t *cfg = config_get_all();
    assert(cfg->spindle_ppr == 2000);
    // Should also recalculate derived values
    assert(cfg->spindle_counts_per_rev == 8000); // 2000 * 4
}

TEST(test_kv_set_float) {
    config_load();
    assert(config_set("z_leadscrew_pitch_mm", "4.0"));
    const machine_config_t *cfg = config_get_all();
    ASSERT_FLOAT_EQ(cfg->z_leadscrew_pitch_mm, 4.0f, 0.001f);
    // z_steps_per_mm = 1000 * 1.0 / 4.0 = 250
    ASSERT_FLOAT_EQ(cfg->z_steps_per_mm, 250.0f, 0.1f);
}

TEST(test_kv_set_bool) {
    config_load();
    assert(config_set("x_is_diameter", "false"));
    const machine_config_t *cfg = config_get_all();
    assert(cfg->x_is_diameter == false);
}

TEST(test_kv_set_unknown_key) {
    config_load();
    assert(!config_set("nonexistent_key", "42"));
}

TEST(test_save_and_reload) {
    config_load();
    config_set("spindle_ppr", "500");
    config_set("z_leadscrew_pitch_mm", "3.0");
    config_save();

    // Simulate reboot by reloading
    bool loaded = config_load();
    assert(loaded);
    const machine_config_t *cfg = config_get_all();
    assert(cfg->spindle_ppr == 500);
    ASSERT_FLOAT_EQ(cfg->z_leadscrew_pitch_mm, 3.0f, 0.001f);
}

static void list_counter_cb(const char *key, const char *value, void *ctx) {
    (*(int *)ctx)++;
    assert(key != NULL);
    assert(value != NULL);
}

TEST(test_config_list_callback) {
    config_load();
    int count = 0;
    config_list(list_counter_cb, &count);
    assert(count > 0);
    assert(count >= 21); // We have 25 config entries
}

TEST(test_belt_ratio_affects_steps_per_mm) {
    reset_flash();
    config_load();
    config_set("z_belt_ratio", "2.0");
    const machine_config_t *cfg = config_get_all();
    // z_steps_per_mm = 1000 * 2.0 / 6.0 = 333.333
    ASSERT_FLOAT_EQ(cfg->z_steps_per_mm, 333.333f, 0.1f);
}

// --- Main ---

int main(void) {
    printf("Config tests:\n");
    RUN(test_defaults_loaded);
    RUN(test_derived_values);
    RUN(test_kv_get_uint16);
    RUN(test_kv_get_float);
    RUN(test_kv_get_bool);
    RUN(test_kv_get_unknown_key);
    RUN(test_kv_set_uint16);
    RUN(test_kv_set_float);
    RUN(test_kv_set_bool);
    RUN(test_kv_set_unknown_key);
    RUN(test_save_and_reload);
    RUN(test_config_list_callback);
    RUN(test_belt_ratio_affects_steps_per_mm);
    printf("All config tests passed!\n\n");
    return 0;
}
