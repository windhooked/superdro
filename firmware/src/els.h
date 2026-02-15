#ifndef ELS_H
#define ELS_H

#include <stdint.h>
#include <stdbool.h>

// ELS sub-states (mapped to machine_state_t in main.c)
typedef enum {
    ELS_IDLE = 0,        // Not engaged, stepper disabled
    ELS_ENGAGED,         // Synchronizing Z steps to spindle
    ELS_FEED_HOLD,       // Paused, stepper enabled (holds position)
} els_state_t;

// Initialize ELS module (call after encoder_init + stepper_init)
void els_init(void);

// Set thread pitch in mm. Rejected if engaged or invalid.
bool els_set_pitch(float pitch_mm);

// Get currently configured pitch (0 if not set)
float els_get_pitch(void);

// Engage: snap origins, enable stepper, begin synchronization.
// Returns false if pitch not set or already engaged.
bool els_engage(void);

// Disengage: stop sync, disable stepper. Safe from any state.
void els_disengage(void);

// Feed hold: stop pushing steps, stepper holds position with torque.
// Only from ENGAGED; no-op otherwise.
void els_feed_hold(void);

// Resume from feed hold. Re-snaps origins to prevent lurch.
// Only from FEED_HOLD; no-op otherwise.
void els_resume(void);

// Get current ELS state
els_state_t els_get_state(void);

// Get tracking error in steps (target - actual)
int32_t els_get_error(void);

// Core 0 sync loop — call every ~20us from real-time loop
void els_update(void);

#endif // ELS_H
