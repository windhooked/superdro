# Phase 1 Integration Test Checklist

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
