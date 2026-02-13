#include "protocol.h"
#include "config.h"
#include "encoder.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"

#define RX_BUF_SIZE 256
#define TX_BUF_SIZE 512

static char rx_buf[RX_BUF_SIZE];
static size_t rx_pos;

void protocol_init(void) {
    rx_pos = 0;
}

// Minimal JSON helpers — no external dependency
static void json_send(const char *json) {
    printf("%s\n", json);
}

void protocol_send_ack(const char *cmd, bool ok, const char *err) {
    char buf[TX_BUF_SIZE];
    if (ok) {
        snprintf(buf, sizeof(buf), "{\"ack\":\"%s\",\"ok\":true}", cmd);
    } else {
        snprintf(buf, sizeof(buf), "{\"ack\":\"%s\",\"ok\":false,\"err\":\"%s\"}", cmd, err ? err : "unknown");
    }
    json_send(buf);
}

void protocol_send_status(const status_snapshot_t *s) {
    char buf[TX_BUF_SIZE];
    const char *state_str;
    switch (s->state) {
        case STATE_IDLE:      state_str = "idle"; break;
        case STATE_JOGGING:   state_str = "jogging"; break;
        case STATE_THREADING: state_str = "threading"; break;
        case STATE_CYCLE:     state_str = "cycle"; break;
        case STATE_FEED_HOLD: state_str = "feed_hold"; break;
        case STATE_ALARM:     state_str = "alarm"; break;
        default:              state_str = "unknown"; break;
    }

    snprintf(buf, sizeof(buf),
        "{\"pos\":{\"x\":%.3f,\"z\":%.3f},\"rpm\":%.0f,\"state\":\"%s\",\"fh\":%s}",
        s->x_pos_mm, s->z_pos_mm, s->rpm, state_str,
        s->feed_hold ? "true" : "false");
    json_send(buf);
}

// Minimal JSON field extraction (avoids full parser for Phase 1)
static bool json_get_string(const char *json, const char *key, char *out, size_t out_len) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char *start = strstr(json, search);
    if (!start) return false;
    start += strlen(search);
    const char *end = strchr(start, '"');
    if (!end) return false;
    size_t len = (size_t)(end - start);
    if (len >= out_len) len = out_len - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

static bool json_get_float(const char *json, const char *key, float *out) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *start = strstr(json, search);
    if (!start) return false;
    start += strlen(search);
    // Skip whitespace and quotes
    while (*start == ' ' || *start == '"') start++;
    *out = strtof(start, NULL);
    return true;
}

static void handle_command(const char *json) {
    char cmd[32];
    if (!json_get_string(json, "cmd", cmd, sizeof(cmd))) {
        return;
    }

    if (strcmp(cmd, "zero") == 0) {
        char axis[4];
        if (json_get_string(json, "axis", axis, sizeof(axis))) {
            if (strcmp(axis, "x") == 0) {
                x_axis_zero();
                protocol_send_ack("zero", true, NULL);
            } else if (strcmp(axis, "z") == 0) {
                // Z zero — placeholder for Phase 1
                protocol_send_ack("zero", true, NULL);
            } else {
                protocol_send_ack("zero", false, "unknown axis");
            }
        }
    } else if (strcmp(cmd, "preset") == 0) {
        char axis[4];
        float value;
        if (json_get_string(json, "axis", axis, sizeof(axis)) &&
            json_get_float(json, "value", &value)) {
            if (strcmp(axis, "x") == 0) {
                x_axis_preset(value);
                protocol_send_ack("preset", true, NULL);
            } else {
                protocol_send_ack("preset", false, "unsupported axis");
            }
        }
    } else if (strcmp(cmd, "config_get") == 0) {
        char key[64], value[32];
        if (json_get_string(json, "key", key, sizeof(key))) {
            if (config_get(key, value, sizeof(value))) {
                char buf[TX_BUF_SIZE];
                snprintf(buf, sizeof(buf),
                    "{\"ack\":\"config_get\",\"ok\":true,\"key\":\"%s\",\"value\":%s}",
                    key, value);
                json_send(buf);
            } else {
                protocol_send_ack("config_get", false, "unknown key");
            }
        }
    } else if (strcmp(cmd, "config_set") == 0) {
        char key[64], value[32];
        if (json_get_string(json, "key", key, sizeof(key))) {
            // Try string value first, then numeric
            if (!json_get_string(json, "value", value, sizeof(value))) {
                float fval;
                if (json_get_float(json, "value", &fval)) {
                    snprintf(value, sizeof(value), "%g", fval);
                } else {
                    protocol_send_ack("config_set", false, "missing value");
                    return;
                }
            }
            if (config_set(key, value)) {
                protocol_send_ack("config_set", true, NULL);
            } else {
                protocol_send_ack("config_set", false, "unknown key");
            }
        }
    } else if (strcmp(cmd, "config_save") == 0) {
        config_save();
        protocol_send_ack("config_save", true, NULL);
    } else if (strcmp(cmd, "config_list") == 0) {
        // Stream all config as a single JSON object
        char buf[TX_BUF_SIZE];
        int pos = snprintf(buf, sizeof(buf), "{\"ack\":\"config_list\",\"ok\":true,\"params\":{");

        typedef struct { char *buf; int pos; size_t size; bool first; } list_ctx_t;
        list_ctx_t ctx = { buf, pos, sizeof(buf), true };

        config_list(
            (config_list_cb)(void (*)(const char *, const char *, void *))
            NULL, // We'll inline this instead
            &ctx
        );

        // Inline approach: iterate config_list with a simple callback
        // For Phase 1, send a simplified response
        char val[32];
        bool first = true;
        const char *keys[] = {
            "spindle_ppr", "spindle_max_rpm",
            "z_leadscrew_pitch_mm", "z_steps_per_rev", "z_belt_ratio", "z_steps_per_mm",
            "x_scale_resolution_mm", "x_is_diameter",
            NULL
        };
        for (int i = 0; keys[i]; i++) {
            if (config_get(keys[i], val, sizeof(val))) {
                pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "%s\"%s\":%s", first ? "" : ",", keys[i], val);
                first = false;
            }
        }
        pos += snprintf(buf + pos, sizeof(buf) - pos, "}}");
        json_send(buf);
    } else {
        protocol_send_ack(cmd, false, "unknown command");
    }
}

void protocol_process_rx(void) {
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\n' || c == '\r') {
            if (rx_pos > 0) {
                rx_buf[rx_pos] = '\0';
                handle_command(rx_buf);
                rx_pos = 0;
            }
        } else if (rx_pos < RX_BUF_SIZE - 1) {
            rx_buf[rx_pos++] = (char)c;
        }
    }
}
