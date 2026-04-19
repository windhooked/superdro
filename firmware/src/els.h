#ifndef ELS_H
#define ELS_H

#include <stdint.h>
#include <stdbool.h>
#include "els_fsm.h"   // re-exports els_state_t, els_fault_code_t, els_status_t

// ---- Backward-compatible aliases for main.c / protocol.c ----
#define ELS_IDLE       ELS_STATE_IDLE
#define ELS_ENGAGED    ELS_STATE_THREADING_ENGAGED
#define ELS_FEED_HOLD  ELS_STATE_THREADING_HOLD

// Lifecycle (called from main.c)
void        els_init(void);
void        els_update(void);   // Core 0 hot loop — calls els_fsm_step + publish

// Legacy single-axis threading API (protocol.c compatibility)
bool        els_set_pitch(float pitch_mm);
float       els_get_pitch(void);
bool        els_engage(void);       // arms threading on first entry in thread table
void        els_disengage(void);
void        els_feed_hold(void);
void        els_resume(void);
els_state_t els_get_state(void);
int32_t     els_get_error(void);    // returns z_backlog (closest analog)

// ---- Extended API ----
bool els_arm_threading(int64_t ratio_num, int64_t ratio_den,
                       uint8_t starts, uint8_t start_index);
bool els_start_feed(uint32_t feed_um_per_rev, uint8_t axes);
bool els_jog_start(stepper_axis_t axis, int8_t direction);
void els_jog_stop(stepper_axis_t axis);
bool els_index_to(uint16_t angle_tenths);
void els_reset_fault(void);

// Status (Core 1 reads)
const els_status_t *els_status_read(void);

#endif // ELS_H
