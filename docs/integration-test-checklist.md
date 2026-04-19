# Integration Test Checklist

## Prerequisites
- [ ] Pico W flashed with `superdro.uf2`
- [ ] Android tablet with SuperDRO app installed
- [ ] USB-C/A cable connecting Pico W to tablet
- [ ] Spindle encoder wired to GP2/GP3/GP4
- [ ] X-axis scale wired to GP5/GP6
- [ ] E-stop button wired to GP14 (NC, active low)
- [ ] Engage/feed hold/cycle start buttons on GP15/GP16/GP17

## Connection
- [ ] App detects Pico W on USB connection
- [ ] Connection indicator shows green "USB Connected"
- [ ] Reconnect after USB disconnect/reconnect
- [ ] No crash on rapid connect/disconnect

## DRO Display
- [ ] X position updates when scale moves
- [ ] Z position displays (placeholder for Phase 1)
- [ ] RPM updates when spindle turns
- [ ] RPM reads 0 when spindle stopped
- [ ] RPM reading is stable (no flicker at constant speed)
- [ ] 50 Hz update rate feels smooth

## Zero / Preset
- [ ] Touch ZERO on X axis — X reads 0.000
- [ ] Touch ZERO on Z axis — Z reads 0.000
- [ ] Touch SET on X axis — enter value — X reads entered value
- [ ] Preset handles negative values correctly

## Unit Conversion
- [ ] mm/in toggle converts displayed values
- [ ] DIA/RAD toggle doubles/halves X reading
- [ ] Zero works correctly in both unit modes
- [ ] Preset accepts values in current display unit

## Configuration
- [ ] CONFIG tab loads parameters from Pico W
- [ ] Editing a value marks it as dirty
- [ ] SAVE sends changed values and persists to flash
- [ ] Derived values (z_steps_per_mm) update after save
- [ ] Config survives Pico W power cycle

## Safety
- [ ] E-stop triggers ALARM state on tablet display
- [ ] LED blinks fast during alarm, slow heartbeat normally
- [ ] Releasing E-stop allows alarm clear

## Edge Cases
- [ ] App handles malformed JSON from Pico gracefully
- [ ] Very high RPM (>3000) displays correctly
- [ ] Large position values (>400mm) display correctly
- [ ] Negative positions display with sign

---

## Phase 2 — ELS (requires hardware + logic analyser)

See `docs/els-bring-up.md` for the full step-by-step procedure. This section is a condensed pass/fail checklist.

### Prerequisites
- [ ] CL57T closed-loop stepper wired to GP8 (step) / GP9 (dir) / GP10 (enable)
- [ ] Spindle encoder on GP2/GP3 (quad) / GP4 (index), 1000+ PPR optical
- [ ] LS 486C X-axis scale: Ua1+→GP5, Ua2+→GP6, Ua0+→GP7 via 1kΩ/2kΩ dividers
      (or YL-128 MAX490 modules × 3 for proper RS-422 receive; 3.3V VCC bench, 5V production)
- [ ] E-stop wired to GP14 (active low)
- [ ] Engage / feed-hold / cycle-start buttons on GP15 / GP16 / GP17
- [ ] Logic analyser or oscilloscope available for step pulse verification

### Step 1 — Spindle encoder ring buffer
- [ ] At 300 RPM, `spindle_read_rate_eps()` returns plausible value (within 5%)
- [ ] Index pulse fires once per revolution
- [ ] `spindle_index_fault()` triggers if index is lost for >2 revolutions

### Step 2 — Single-start threading (M3×0.5)
- [ ] `els_set_pitch(0.5)` + `els_engage()` → state = THREADING_ARMED
- [ ] First index pulse → state transitions to THREADING_ENGAGED
- [ ] Step pulses visible on LA at correct rate relative to spindle
- [ ] `els_disengage()` stops Z motion, state = IDLE

### Step 3 — Soft limits
- [ ] Carriage at Z_min: further threading attempt faults with SOFT_LIMIT
- [ ] RESET clears fault, returns to IDLE

### Step 4 — Feed hold / resume
- [ ] `els_feed_hold()` while threading → state = THREADING_HOLD, Z stops
- [ ] `els_resume()` → state = THREADING_ENGAGED, Z resumes in sync

### Step 5 — Multi-start threads (2-start M3×1)
- [ ] `CMD_SET_STARTS` with starts=2, start_idx=0 then start_idx=1 produce
      helices offset by exactly 180° (verify with thread gauge or LA)

### Step 6 — Power feed
- [ ] `CMD_START_FEED` on Z axis: carriage moves at correct mm/rev
- [ ] Reversal (spindle direction change) reverses carriage

### Step 7 — Jog
- [ ] `CMD_JOG_START` / `CMD_JOG_STOP` on Z: smooth accel/decel ramp
- [ ] No step pulses after `CMD_JOG_STOP` completes

### Step 8 — E-stop
- [ ] Trigger E-stop during threading → state = FAULT, Z stops immediately
- [ ] `CMD_RESET_FAULT` clears to IDLE
