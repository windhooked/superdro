#include "protocol.h"
#include "config.h"
#include "encoder.h"
#include "els.h"
#include "els_fsm.h"   // CMD_* opcodes, els_fsm_event
#include "stepper.h"   // stepper_axis_t
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"

#define RX_BUF_SIZE 256
#define TX_BUF_SIZE 1024   // enlarged for config_list with ELS keys

static char rx_buf[RX_BUF_SIZE];
static size_t rx_pos;

void protocol_init(void) {
    rx_pos = 0;
}

// ---- Minimal JSON helpers — no external dependency ----

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
    while (*start == ' ' || *start == '"') start++;
    *out = strtof(start, NULL);
    return true;
}

static bool json_get_int(const char *json, const char *key, int32_t *out) {
    float f;
    if (!json_get_float(json, key, &f)) return false;
    *out = (int32_t)f;
    return true;
}

// ---- ELS state/fault → string ----

static const char *els_state_str(els_state_t st) {
    switch (st) {
        case ELS_STATE_IDLE:               return "idle";
        case ELS_STATE_THREADING_ARMED:    return "threading_armed";
        case ELS_STATE_THREADING_ENGAGED:  return "threading_engaged";
        case ELS_STATE_THREADING_HOLD:     return "threading_hold";
        case ELS_STATE_FEED_ENGAGED:       return "feed_engaged";
        case ELS_STATE_JOG:                return "jog";
        case ELS_STATE_TAPER_ENGAGED:      return "taper_engaged";
        case ELS_STATE_INDEXING:           return "indexing";
        case ELS_STATE_FAULT:              return "fault";
        default:                           return "unknown";
    }
}

static const char *els_fault_str(els_fault_code_t f) {
    switch (f) {
        case ELS_FAULT_NONE:            return "none";
        case ELS_FAULT_BACKLOG:         return "backlog";
        case ELS_FAULT_SOFT_LIMIT:      return "soft_limit";
        case ELS_FAULT_RATE_EXCEEDED:   return "rate_exceeded";
        case ELS_FAULT_REVERSAL:        return "reversal";
        case ELS_FAULT_INDEX_LOST:      return "index_lost";
        case ELS_FAULT_ESTOP:           return "estop";
        case ELS_FAULT_BAD_RATIO:       return "bad_ratio";
        case ELS_FAULT_MULTISTART_PPR:  return "multistart_ppr";
        default:                        return "unknown";
    }
}

// ---- Status serialisation ----

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
        "{\"pos\":{\"x\":%.3f,\"z\":%.3f},\"rpm\":%.0f,\"state\":\"%s\","
        "\"fh\":%s,\"pitch\":%.3f,"
        "\"els_state\":\"%s\",\"els_fault\":\"%s\","
        "\"z_backlog\":%ld,\"x_backlog\":%ld,"
        "\"spindle_count\":%ld,\"c_pos\":%ld,"
        "\"index_latched\":%s,\"estop\":%s}",
        s->x_pos_mm, s->z_pos_mm, s->rpm, state_str,
        s->feed_hold ? "true" : "false",
        s->pitch_mm,
        els_state_str(s->els_state),
        els_fault_str(s->els_fault),
        (long)s->z_backlog, (long)s->x_backlog,
        (long)s->spindle_count, (long)s->c_pos_steps,
        s->index_latched ? "true" : "false",
        s->estop ? "true" : "false");
    json_send(buf);
}

// ---- Command dispatch ----

static void handle_command(const char *json) {
    char cmd[32];
    if (!json_get_string(json, "cmd", cmd, sizeof(cmd))) {
        return;
    }

    // ---- DRO commands (Phase 1) ----

    if (strcmp(cmd, "zero") == 0) {
        char axis[4];
        if (json_get_string(json, "axis", axis, sizeof(axis))) {
            if (strcmp(axis, "x") == 0) {
                x_axis_zero();
                protocol_send_ack("zero", true, NULL);
            } else if (strcmp(axis, "z") == 0) {
                z_axis_zero();
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
            } else if (strcmp(axis, "z") == 0) {
                z_axis_preset(value);
                protocol_send_ack("preset", true, NULL);
            } else {
                protocol_send_ack("preset", false, "unsupported axis");
            }
        }

    // ---- Config commands ----

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
        char buf[TX_BUF_SIZE];
        int pos = snprintf(buf, sizeof(buf),
            "{\"ack\":\"config_list\",\"ok\":true,\"params\":{");
        char val[32];
        bool first = true;
        static const char *keys[] = {
            // DRO / spindle
            "spindle_ppr", "spindle_max_rpm",
            "z_scale_resolution_mm", "z_leadscrew_pitch_mm",
            "z_steps_per_rev", "z_belt_ratio", "z_steps_per_mm",
            "x_scale_resolution_mm", "x_is_diameter",
            // ELS — step rates
            "z_max_step_rate", "x_max_step_rate", "c_max_step_rate",
            // ELS — soft limits
            "z_soft_min_steps", "z_soft_max_steps",
            "x_soft_min_steps", "x_soft_max_steps",
            // ELS — backlog
            "z_backlog_threshold", "x_backlog_threshold",
            // ELS — ramp
            "z_ramp_min_delay", "z_ramp_max_delay", "z_ramp_delta",
            "x_ramp_min_delay", "x_ramp_max_delay", "x_ramp_delta",
            "c_ramp_min_delay", "c_ramp_delta",
            // ELS — C-axis
            "c_steps_per_rev", "c_pulse_width_us",
            "thread_table_count",
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

    // ---- ELS legacy commands (Phase 1 compat) ----

    } else if (strcmp(cmd, "set_pitch") == 0) {
        float pitch;
        if (json_get_float(json, "pitch", &pitch)) {
            if (els_set_pitch(pitch)) {
                protocol_send_ack("set_pitch", true, NULL);
            } else {
                protocol_send_ack("set_pitch", false, "invalid pitch or would exceed max step rate");
            }
        } else {
            protocol_send_ack("set_pitch", false, "missing pitch");
        }

    } else if (strcmp(cmd, "els_engage") == 0) {
        if (els_engage()) {
            protocol_send_ack("els_engage", true, NULL);
        } else {
            protocol_send_ack("els_engage", false, "cannot engage");
        }

    } else if (strcmp(cmd, "els_disengage") == 0) {
        els_disengage();
        protocol_send_ack("els_disengage", true, NULL);

    } else if (strcmp(cmd, "els_feed_hold") == 0) {
        els_feed_hold();
        protocol_send_ack("els_feed_hold", true, NULL);

    } else if (strcmp(cmd, "els_resume") == 0) {
        els_resume();
        protocol_send_ack("els_resume", true, NULL);

    // ---- ELS Phase 2 commands ----

    // arm: {"cmd":"arm","pitch":1.0,"starts":1,"start_index":0}
    } else if (strcmp(cmd, "arm") == 0) {
        float pitch = 0.0f;
        int32_t starts = 1, start_idx = 0;
        if (!json_get_float(json, "pitch", &pitch)) {
            protocol_send_ack("arm", false, "missing pitch");
            return;
        }
        json_get_int(json, "starts", &starts);
        json_get_int(json, "start_index", &start_idx);
        if (!els_set_pitch(pitch)) {
            protocol_send_ack("arm", false, "invalid pitch");
            return;
        }
        const machine_config_t *cfg = config_get_all();
        int64_t num = (int64_t)(pitch * 10000.0f + 0.5f) * (int64_t)cfg->z_steps_per_rev;
        int64_t den = (int64_t)(cfg->z_leadscrew_pitch_mm * 10000.0f + 0.5f)
                      * (int64_t)cfg->spindle_counts_per_rev;
        if (els_arm_threading(num, den, (uint8_t)starts, (uint8_t)start_idx)) {
            protocol_send_ack("arm", true, NULL);
        } else {
            protocol_send_ack("arm", false, "arm failed");
        }

    // disarm: {"cmd":"disarm"}
    } else if (strcmp(cmd, "disarm") == 0) {
        els_fsm_event(CMD_DISARM, 0);
        protocol_send_ack("disarm", true, NULL);

    // disengage: {"cmd":"disengage"}
    } else if (strcmp(cmd, "disengage") == 0) {
        els_disengage();
        protocol_send_ack("disengage", true, NULL);

    // feed_hold: {"cmd":"feed_hold"}
    } else if (strcmp(cmd, "feed_hold") == 0) {
        els_feed_hold();
        protocol_send_ack("feed_hold", true, NULL);

    // resume: {"cmd":"resume"}
    } else if (strcmp(cmd, "resume") == 0) {
        els_resume();
        protocol_send_ack("resume", true, NULL);

    // feed: {"cmd":"feed","um_rev":100,"axes":1}  axes bitmask: 1=Z, 2=X, 3=both
    } else if (strcmp(cmd, "feed") == 0) {
        int32_t um_rev = 0, axes = 1;
        if (!json_get_int(json, "um_rev", &um_rev) || um_rev <= 0) {
            protocol_send_ack("feed", false, "missing or invalid um_rev");
            return;
        }
        json_get_int(json, "axes", &axes);
        if (els_start_feed((uint32_t)um_rev, (uint8_t)axes)) {
            protocol_send_ack("feed", true, NULL);
        } else {
            protocol_send_ack("feed", false, "feed failed");
        }

    // jog: {"cmd":"jog","axis":0,"dir":1}  axis: 0=Z, 1=X, 2=C
    } else if (strcmp(cmd, "jog") == 0) {
        int32_t axis = 0, dir = 1;
        json_get_int(json, "axis", &axis);
        json_get_int(json, "dir", &dir);
        if (els_jog_start((stepper_axis_t)axis, (int8_t)dir)) {
            protocol_send_ack("jog", true, NULL);
        } else {
            protocol_send_ack("jog", false, "jog failed");
        }

    // jog_stop: {"cmd":"jog_stop"} or {"cmd":"jog_stop","axis":0}
    } else if (strcmp(cmd, "jog_stop") == 0) {
        int32_t axis = 0;
        json_get_int(json, "axis", &axis);
        els_jog_stop((stepper_axis_t)axis);
        protocol_send_ack("jog_stop", true, NULL);

    // index: {"cmd":"index","angle_tenths":1800}  (0.1° units; 1800 = 180°)
    } else if (strcmp(cmd, "index") == 0) {
        int32_t angle = 0;
        if (!json_get_int(json, "angle_tenths", &angle)) {
            protocol_send_ack("index", false, "missing angle_tenths");
            return;
        }
        if (els_index_to((uint16_t)angle)) {
            protocol_send_ack("index", true, NULL);
        } else {
            protocol_send_ack("index", false, "index failed");
        }

    // set_starts: {"cmd":"set_starts","starts":2,"start_index":1}
    } else if (strcmp(cmd, "set_starts") == 0) {
        int32_t starts = 1, start_idx = 0;
        json_get_int(json, "starts", &starts);
        json_get_int(json, "start_index", &start_idx);
        uint32_t payload = ((uint32_t)(starts & 0xFF) << 8) | (uint32_t)(start_idx & 0xFF);
        els_fsm_event(CMD_SET_STARTS, payload);
        protocol_send_ack("set_starts", true, NULL);

    // reset_fault: {"cmd":"reset_fault"}
    } else if (strcmp(cmd, "reset_fault") == 0) {
        els_reset_fault();
        protocol_send_ack("reset_fault", true, NULL);

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
