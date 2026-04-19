#ifndef ELS_FSM_H
#define ELS_FSM_H

#include <stdint.h>
#include <stdbool.h>
#include "els_engine.h"  // for stepper_axis_t

typedef enum {
    ELS_STATE_IDLE              = 0,
    ELS_STATE_THREADING_ARMED   = 1,
    ELS_STATE_THREADING_ENGAGED = 2,
    ELS_STATE_THREADING_HOLD    = 3,
    ELS_STATE_FEED_ENGAGED      = 4,
    ELS_STATE_JOG               = 5,
    ELS_STATE_TAPER_ENGAGED     = 6,
    ELS_STATE_INDEXING          = 7,
    ELS_STATE_FAULT             = 8,
} els_state_t;

typedef enum {
    ELS_FAULT_NONE          = 0,
    ELS_FAULT_BACKLOG       = 1,
    ELS_FAULT_SOFT_LIMIT    = 2,
    ELS_FAULT_RATE_EXCEEDED = 3,
    ELS_FAULT_REVERSAL      = 4,
    ELS_FAULT_INDEX_LOST    = 5,
    ELS_FAULT_ESTOP         = 6,
    ELS_FAULT_BAD_RATIO     = 7,
    ELS_FAULT_MULTISTART_PPR = 8,
} els_fault_code_t;

// Inter-core command opcodes (packed into uint32: bits[31:24]=opcode, bits[23:0]=payload)
typedef enum {
    CMD_NOP           = 0x00,
    CMD_ARM_THREADING = 0x01, // payload: thread_table index (uint8)
    CMD_START_FEED    = 0x02, // payload: feed_um_rev[23:8] | axes[7:0]
    CMD_START_TAPER   = 0x03, // payload: (ratios pre-set via CMD_SET_RATIO)
    CMD_JOG_START     = 0x04, // payload: axis[5:4] | direction_bit[0]
    CMD_JOG_STOP      = 0x05,
    CMD_INDEX_TO      = 0x06, // payload: angle_tenths (0–3599)
    CMD_DISARM        = 0x07,
    CMD_DISENGAGE     = 0x08,
    CMD_FEED_HOLD     = 0x09,
    CMD_RESUME        = 0x0A,
    CMD_RESET_FAULT   = 0x0B,
    CMD_SET_STARTS    = 0x0C, // payload: starts[15:8] | start_index[7:0]
} els_cmd_opcode_t;

typedef struct {
    els_state_t      state;
    els_fault_code_t fault;
    int32_t          z_pos_steps;
    int32_t          x_pos_steps;
    int32_t          c_pos_steps;
    int32_t          spindle_count;
    float            spindle_rpm;
    int32_t          z_backlog;
    int32_t          x_backlog;
    bool             index_latched;
    bool             estop;
} els_status_t;

// Init FSM and all sub-modules. Call after spindle_init and stepper_init.
void els_fsm_init(void);

// Advance FSM by one tick. Core 0 hot loop. __not_in_flash_func.
void els_fsm_step(void);

// Inject event from physical button edge-detect (synchronous, Core 0).
// __not_in_flash_func.
void els_fsm_event(els_cmd_opcode_t cmd, uint32_t payload);

els_state_t      els_fsm_get_state(void);
els_fault_code_t els_fsm_get_fault(void);

// Publish status snapshot (double-buffer swap). Core 0. __not_in_flash_func.
void els_fsm_publish_status(float spindle_rpm);

// Read latest published status. Safe to call from Core 1.
const els_status_t *els_fsm_status_read(void);

#endif // ELS_FSM_H
