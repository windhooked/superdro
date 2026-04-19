#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "els_fsm.h"   // els_state_t, els_fault_code_t

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
    float            x_pos_mm;
    float            z_pos_mm;
    float            rpm;
    machine_state_t  state;
    bool             feed_hold;
    bool             estop;
    float            pitch_mm;       // current thread pitch (0 if not set)
    // ELS Phase 2
    els_state_t      els_state;      // full FSM state
    els_fault_code_t els_fault;      // fault code (valid when els_state == ELS_STATE_FAULT)
    int32_t          z_backlog;      // Z step backlog
    int32_t          x_backlog;      // X step backlog
    int32_t          spindle_count;  // raw spindle encoder count
    int32_t          c_pos_steps;    // C-axis position in steps
    bool             index_latched;  // true when index latch is pending
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
