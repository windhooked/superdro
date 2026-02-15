#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

// Machine state reported to Android
typedef enum {
    STATE_IDLE = 0,
    STATE_JOGGING,
    STATE_THREADING,
    STATE_CYCLE,
    STATE_FEED_HOLD,
    STATE_ALARM,
} machine_state_t;

// Shared status snapshot (written by Core 0, read by Core 1)
typedef struct {
    float x_pos_mm;
    float z_pos_mm;
    float rpm;
    machine_state_t state;
    bool feed_hold;
    bool estop;
    // ELS fields
    float pitch_mm;        // Current thread pitch (0 if not set)
    uint8_t els_state;     // 0=idle, 1=engaged, 2=feed_hold
    int32_t els_error;     // Tracking error in steps
} status_snapshot_t;

// Initialize USB CDC serial
void protocol_init(void);

// Process incoming serial commands (called from Core 1 loop)
void protocol_process_rx(void);

// Send status JSON at ~50 Hz (called from Core 1 loop)
void protocol_send_status(const status_snapshot_t *status);

// Send an ACK/NACK response
void protocol_send_ack(const char *cmd, bool ok, const char *err);

#endif // PROTOCOL_H
