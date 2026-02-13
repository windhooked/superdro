#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

// Flash storage: last sector of flash
#define CONFIG_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define CONFIG_MAGIC        0x5344524F  // "SDRO"
#define CONFIG_VERSION      1

typedef struct {
    uint32_t magic;
    uint32_t version;
    machine_config_t config;
    uint32_t checksum;
} config_flash_t;

static machine_config_t g_config;

static void config_set_defaults(void) {
    g_config = (machine_config_t){
        .spindle_ppr            = 1000,
        .spindle_quadrature     = 4,
        .spindle_counts_per_rev = 4000,
        .spindle_max_rpm        = 3500,

        .z_leadscrew_pitch_mm   = 6.0f,
        .z_steps_per_rev        = 1000,
        .z_belt_ratio           = 1.0f,
        .z_steps_per_mm         = 0.0f,    // derived
        .z_max_speed_mm_s       = 50.0f,
        .z_accel_mm_s2          = 100.0f,
        .z_backlash_mm          = 0.0f,
        .z_travel_min_mm        = -500.0f,
        .z_travel_max_mm        = 0.0f,

        .x_scale_resolution_mm  = 0.005f,
        .x_is_diameter          = true,
        .x_travel_min_mm        = -200.0f,
        .x_travel_max_mm        = 0.0f,

        .x_steps_per_rev        = 1000,
        .x_leadscrew_pitch_mm   = 3.0f,
        .x_belt_ratio           = 1.0f,
        .x_steps_per_mm         = 0.0f,    // derived

        .thread_retract_mode    = 0,
        .thread_retract_x_mm    = 1.0f,
        .thread_compound_angle  = 29.5f,
    };
}

static uint32_t config_checksum(const machine_config_t *cfg) {
    const uint8_t *p = (const uint8_t *)cfg;
    uint32_t sum = 0;
    for (size_t i = 0; i < sizeof(*cfg); i++) {
        sum += p[i];
    }
    return sum;
}

void config_recalculate(void) {
    if (g_config.z_leadscrew_pitch_mm > 0.0f) {
        g_config.z_steps_per_mm = (float)g_config.z_steps_per_rev
            * g_config.z_belt_ratio / g_config.z_leadscrew_pitch_mm;
    }
    if (g_config.x_leadscrew_pitch_mm > 0.0f) {
        g_config.x_steps_per_mm = (float)g_config.x_steps_per_rev
            * g_config.x_belt_ratio / g_config.x_leadscrew_pitch_mm;
    }
    g_config.spindle_counts_per_rev =
        (uint32_t)g_config.spindle_ppr * g_config.spindle_quadrature;
}

bool config_load(void) {
    const config_flash_t *stored =
        (const config_flash_t *)(XIP_BASE + CONFIG_FLASH_OFFSET);

    if (stored->magic == CONFIG_MAGIC &&
        stored->version == CONFIG_VERSION &&
        stored->checksum == config_checksum(&stored->config)) {
        memcpy(&g_config, &stored->config, sizeof(g_config));
        config_recalculate();
        return true;
    }

    config_set_defaults();
    config_recalculate();
    return false;
}

bool config_save(void) {
    config_flash_t block;
    block.magic = CONFIG_MAGIC;
    block.version = CONFIG_VERSION;
    memcpy(&block.config, &g_config, sizeof(g_config));
    block.checksum = config_checksum(&g_config);

    // Pad to page size
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &block, sizeof(block));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(CONFIG_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CONFIG_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    return true;
}

const machine_config_t *config_get_all(void) {
    return &g_config;
}

// Key-value access table
typedef struct {
    const char *key;
    enum { KV_U16, KV_U8, KV_U32, KV_FLOAT, KV_BOOL } type;
    size_t offset;
} config_kv_entry_t;

#define KV_ENTRY(name, t) { #name, t, offsetof(machine_config_t, name) }

static const config_kv_entry_t kv_table[] = {
    KV_ENTRY(spindle_ppr,            KV_U16),
    KV_ENTRY(spindle_quadrature,     KV_U8),
    KV_ENTRY(spindle_counts_per_rev, KV_U32),
    KV_ENTRY(spindle_max_rpm,        KV_U16),

    KV_ENTRY(z_leadscrew_pitch_mm,   KV_FLOAT),
    KV_ENTRY(z_steps_per_rev,        KV_U16),
    KV_ENTRY(z_belt_ratio,           KV_FLOAT),
    KV_ENTRY(z_steps_per_mm,         KV_FLOAT),
    KV_ENTRY(z_max_speed_mm_s,       KV_FLOAT),
    KV_ENTRY(z_accel_mm_s2,          KV_FLOAT),
    KV_ENTRY(z_backlash_mm,          KV_FLOAT),
    KV_ENTRY(z_travel_min_mm,        KV_FLOAT),
    KV_ENTRY(z_travel_max_mm,        KV_FLOAT),

    KV_ENTRY(x_scale_resolution_mm,  KV_FLOAT),
    KV_ENTRY(x_is_diameter,          KV_BOOL),
    KV_ENTRY(x_travel_min_mm,        KV_FLOAT),
    KV_ENTRY(x_travel_max_mm,        KV_FLOAT),

    KV_ENTRY(x_steps_per_rev,        KV_U16),
    KV_ENTRY(x_leadscrew_pitch_mm,   KV_FLOAT),
    KV_ENTRY(x_belt_ratio,           KV_FLOAT),
    KV_ENTRY(x_steps_per_mm,         KV_FLOAT),

    KV_ENTRY(thread_retract_mode,    KV_U8),
    KV_ENTRY(thread_retract_x_mm,    KV_FLOAT),
    KV_ENTRY(thread_compound_angle,  KV_FLOAT),
};

#define KV_TABLE_SIZE (sizeof(kv_table) / sizeof(kv_table[0]))

static void *kv_ptr(const config_kv_entry_t *e) {
    return (uint8_t *)&g_config + e->offset;
}

bool config_get(const char *key, char *value_out, size_t value_len) {
    for (size_t i = 0; i < KV_TABLE_SIZE; i++) {
        if (strcmp(kv_table[i].key, key) == 0) {
            void *p = kv_ptr(&kv_table[i]);
            switch (kv_table[i].type) {
                case KV_U8:    snprintf(value_out, value_len, "%u", *(uint8_t *)p); break;
                case KV_U16:   snprintf(value_out, value_len, "%u", *(uint16_t *)p); break;
                case KV_U32:   snprintf(value_out, value_len, "%lu", *(uint32_t *)p); break;
                case KV_FLOAT: snprintf(value_out, value_len, "%.4f", *(float *)p); break;
                case KV_BOOL:  snprintf(value_out, value_len, "%s", *(bool *)p ? "true" : "false"); break;
            }
            return true;
        }
    }
    return false;
}

bool config_set(const char *key, const char *value) {
    for (size_t i = 0; i < KV_TABLE_SIZE; i++) {
        if (strcmp(kv_table[i].key, key) == 0) {
            void *p = kv_ptr(&kv_table[i]);
            switch (kv_table[i].type) {
                case KV_U8:    *(uint8_t *)p  = (uint8_t)atoi(value); break;
                case KV_U16:   *(uint16_t *)p = (uint16_t)atoi(value); break;
                case KV_U32:   *(uint32_t *)p = (uint32_t)strtoul(value, NULL, 10); break;
                case KV_FLOAT: *(float *)p    = strtof(value, NULL); break;
                case KV_BOOL:  *(bool *)p     = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0); break;
            }
            config_recalculate();
            return true;
        }
    }
    return false;
}

void config_list(config_list_cb cb, void *ctx) {
    char buf[32];
    for (size_t i = 0; i < KV_TABLE_SIZE; i++) {
        config_get(kv_table[i].key, buf, sizeof(buf));
        cb(kv_table[i].key, buf, ctx);
    }
}
