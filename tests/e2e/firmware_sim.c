// firmware_sim.c — Simulates Pico W firmware serial output for E2E testing
//
// Emits status JSON on stdout (like Pico → Android) and reads commands on stdin.
// Used with a pipe to test the full protocol round-trip.
//
// Compile: gcc -Wall -o firmware_sim firmware_sim.c -I ../../firmware/test/mocks -I ../../firmware/src ../../firmware/src/config.c ../../firmware/src/protocol.c ../../firmware/src/encoder.c ../../firmware/test/mock_flash.c -lm

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "config.h"
#include "protocol.h"

// Simulated machine state
static status_snapshot_t sim_status = {
    .x_pos_mm = 0.0f,
    .z_pos_mm = 0.0f,
    .rpm = 0.0f,
    .state = STATE_IDLE,
    .feed_hold = false,
    .estop = false,
};

// Scenario definitions
typedef struct {
    const char *name;
    float x, z, rpm;
    machine_state_t state;
} scenario_t;

static const scenario_t scenarios[] = {
    {"idle_zero",      0.0f,     0.0f,     0.0f,   STATE_IDLE},
    {"manual_turning", 12.450f,  -35.200f, 820.0f, STATE_IDLE},
    {"high_rpm",       5.0f,     -10.0f,   3500.0f, STATE_IDLE},
    {"alarm",          0.0f,     0.0f,     0.0f,   STATE_ALARM},
    {"negative_pos",   -25.500f, -100.0f,  450.0f, STATE_IDLE},
    {"large_pos",      199.999f, -499.0f,  100.0f, STATE_IDLE},
};

#define NUM_SCENARIOS (sizeof(scenarios) / sizeof(scenarios[0]))

int main(int argc, char *argv[]) {
    config_load();
    config_recalculate();
    protocol_init();

    if (argc > 1 && strcmp(argv[1], "--scenarios") == 0) {
        // Emit all scenarios as status lines
        for (size_t i = 0; i < NUM_SCENARIOS; i++) {
            sim_status.x_pos_mm = scenarios[i].x;
            sim_status.z_pos_mm = scenarios[i].z;
            sim_status.rpm = scenarios[i].rpm;
            sim_status.state = scenarios[i].state;
            protocol_send_status(&sim_status);
            fflush(stdout);
        }
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--commands") == 0) {
        // Read commands from stdin, process them, and output responses
        char line[256];
        while (fgets(line, sizeof(line), stdin)) {
            // Strip newline
            size_t len = strlen(line);
            if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
            if (strlen(line) == 0) continue;

            // Simulate command processing via protocol module
            // We can't call protocol_process_rx directly (it reads from getchar),
            // so we replicate the command handling inline.
            fprintf(stderr, "CMD: %s\n", line);

            // Simple command simulation
            if (strstr(line, "\"cmd\":\"zero\"")) {
                printf("{\"ack\":\"zero\",\"ok\":true}\n");
            } else if (strstr(line, "\"cmd\":\"preset\"")) {
                printf("{\"ack\":\"preset\",\"ok\":true}\n");
            } else if (strstr(line, "\"cmd\":\"config_get\"")) {
                // Extract key and look it up
                char *key_start = strstr(line, "\"key\":\"");
                if (key_start) {
                    key_start += 7;
                    char *key_end = strchr(key_start, '"');
                    if (key_end) {
                        char key[64];
                        size_t klen = (size_t)(key_end - key_start);
                        if (klen < sizeof(key)) {
                            memcpy(key, key_start, klen);
                            key[klen] = '\0';
                            char val[32];
                            if (config_get(key, val, sizeof(val))) {
                                printf("{\"ack\":\"config_get\",\"ok\":true,\"key\":\"%s\",\"value\":%s}\n", key, val);
                            } else {
                                printf("{\"ack\":\"config_get\",\"ok\":false,\"err\":\"unknown key\"}\n");
                            }
                        }
                    }
                }
            } else if (strstr(line, "\"cmd\":\"config_save\"")) {
                printf("{\"ack\":\"config_save\",\"ok\":true}\n");
            } else {
                printf("{\"ack\":\"unknown\",\"ok\":false,\"err\":\"unknown command\"}\n");
            }
            fflush(stdout);
        }
        return 0;
    }

    fprintf(stderr, "Usage: %s --scenarios | --commands\n", argv[0]);
    return 1;
}
