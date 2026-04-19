# ELS Bring-up Procedure

> Companion to [`els-layer-a.md`](els-layer-a.md) and [`els-layer-b.md`](els-layer-b.md). Each step has a concrete pass/fail criterion. Do not advance to the next step until the current one passes. Required equipment: oscilloscope or logic analyzer (LA), USB serial terminal.

---

## Prerequisites

- [ ] Host unit tests pass: `cd firmware/test && make test`
- [ ] Firmware cross-compiles clean: `cd firmware/build && cmake .. -DPICO_BOARD=pico_w && make -j4`
- [ ] Pico W flashed with new firmware
- [ ] USB serial terminal open at 115200 baud (e.g. `screen /dev/cu.usbmodem*`)

---

## Step 1 — Spindle decoder: 3-pin PIO + DMA ring

**Setup:** Encoder connected to GP2 (A), GP3 (B), GP4 (I). Scope channel A on GP2, channel B on GP3, channel C on GP4.

**Procedure:**
1. Rotate spindle by hand through ~5 revolutions forward, then ~5 reverse.
2. In serial terminal, observe the periodic status JSON messages (broadcast automatically at ~50 Hz); watch the `spindle_count` field.

**Pass criteria:**
- Count increases monotonically on forward rotation; decreases on reverse. No missed edges (count tracks exactly with rotation).
- `index_latched` field toggles true once per revolution when GP4 pulse fires, returns false after the next status poll.
- No DMA ring overflow: spin at max RPM for 60 seconds with a signal generator on GP2/GP3 (4096 PPR equivalent); `spindle_count` must be exactly `4096 × 4 × revolutions` at end.

**LA check:** GP2/GP3 transitions at up to 820 kHz show clean edges captured in ring (verify by counting JSON-reported count vs. generator pulse count).

**Fail indicators:** Count jumps backward mid-forward-rotation → AB wiring swap. Count stops changing → PIO or DMA init issue (check `pio_add_program` return code, DMA channel claim).

---

## Step 2 — Index pulse integrity + interval fault

**Setup:** Same wiring. Set `spindle_ppr = 1000` via serial config.

**Procedure:**
1. Spin spindle at steady ~300 RPM for 60 seconds.
2. Manually block/glitch the index signal for one revolution (unplug briefly).

**Pass criteria:**
- During steady spin: `index_latched` cycles true/false once per revolution; no `ELS_FAULT_INDEX_LOST` triggered.
- On glitch: firmware transitions to `ELS_STATE_FAULT` with `fault = ELS_FAULT_INDEX_LOST` within 1–2 revolutions.
- After `{"cmd":"reset_fault"}`, system returns to `ELS_STATE_IDLE` cleanly.

---

## Step 3 — Host unit tests (pure logic, no hardware)

**Procedure:**
```bash
cd firmware/test
make test
```

**Pass criteria:** All 11 test suites exit 0:
- `test_bresenham`: zero drift at 50+ ratios over 1M edges
- `test_ramp`: trapezoidal profile, idle after disengage
- `test_ratio`: GCD reduction, invalid ratio rejection, PPR divisibility
- `test_softlimit`: FAULT returned on limit crossing, no over-emission
- `test_fsm`: all transitions reachable, FAULT sticky, reset works
- `test_config`, `test_encoder`, `test_stepper`, `test_els`, `test_protocol`: regression (existing)

---

## Step 4 — Z stepper: pure-Bresenham mode (predictor bypassed)

**Setup:** Z stepper wired to GP8 (step), GP9 (dir), GP10 (enable). CL57T driver powered. LA channel on GP8 (step pulses).

**Procedure:**
1. Configure a 1.0 mm pitch thread: `{"cmd":"arm", "pitch":1.0, "starts":1, "start_index":0}`
2. Rotate spindle exactly 10 revolutions by hand (count from index pulse).
3. Measure Z travel with a dial indicator.

**Pass criteria:**
- Dial indicator reads exactly `10 × pitch_mm = 10.0 mm` ±0.01 mm.
- LA shows no double-pulses or missed pulses on GP8.
- `z_backlog` in status stays 0 during engagement.

**Fail indicators:** Z travel ≠ 10.0 mm → check `z_steps_per_mm` config, ratio computation in `els_engine_axis_init`. Backlog climbs → FIFO drain rate issue.

---

## Step 5 — Z stepper: predictor + ±10% step-interval variance

**Setup:** Same as Step 4. LA triggered on GP8 step pulses, capture 1000 consecutive pulses at steady ~300 RPM.

**Procedure:**
1. Arm and engage threading at 1.0 mm pitch.
2. Spin spindle at steady RPM for 10+ revolutions.
3. Export LA capture; compute step-interval statistics.

**Pass criteria:**
- Mean step interval matches `60 / (RPM × steps_per_rev)` ±5%.
- Standard deviation of step interval < 10% of mean (the ±10% variance requirement from PRD §0).
- `z_backlog` stays 0 throughout.

**Reference calculation:** At 300 RPM, 1.0 mm pitch, 167 steps/mm, steps/rev = 167: step rate = 300/60 × 167 = 835 steps/sec, interval ≈ 1.20 ms. Spec: σ < 0.12 ms.

---

## Step 6 — Ramp generator: jog mode

**Setup:** Same Z stepper. LA on GP8.

**Procedure:**
1. Send `{"cmd":"jog", "axis":0, "dir":1}` — Z jog forward.
2. After 2 seconds, send `{"cmd":"jog_stop"}`.
3. Observe LA capture of step-pulse train.

**Pass criteria:**
- LA shows clear trapezoidal profile: accel ramp up, plateau, decel ramp down, full stop.
- No step pulses after decel completes (motor holds torque silently).
- Final `z_backlog` = 0.

---

## Step 7 — Thread-start synchronization (single-start)

**Setup:** Thin marker line on spindle shaft aligned with index sensor.

**Procedure:**
1. Arm threading: `{"cmd":"arm", "pitch":1.0, "starts":1, "start_index":0}`
2. Engage 5 times in succession. Between engagements, rotate spindle ~3 revolutions; re-arm each time.
3. For each engagement, mark the starting scratch on a test workpiece.

**Pass criteria:**
- All 5 start marks land within ±1 encoder edge of each other on the workpiece (test with dial indicator or digital caliper across scratch marks).
- `index_latched` transitions from false to true exactly once per engagement cycle before Z starts moving.

---

## Step 8 — Multi-start thread synchronization

**Setup:** Same as Step 7. Configure 2-start thread.

**Procedure:**
1. Send `{"cmd":"set_starts", "starts":2, "start_index":0}`; arm and cut pass 0 (index at 0°).
2. Send `{"cmd":"set_starts", "starts":2, "start_index":1}`; arm and cut pass 1 (index at 180°).
3. Inspect threads under magnification.

**Pass criteria:**
- Two helical starts are evenly spaced (180° ± ~0.5°, equivalent to ±PPR/720 counts).
- `{"cmd":"set_starts", "starts":3, "start_index":0}` with `spindle_ppr` not divisible by 3 must produce `ELS_FAULT_MULTISTART_PPR` immediately on arm.

---

## Step 9 — JSON protocol integration (all modes)

**Setup:** Android app or webapp connected over USB.

**Procedure:**
1. Exercise each command from the UI: arm/engage/disengage, feed-hold/resume, jog, feed mode, reset-fault.
2. Observe state transitions in status JSON stream.

**Pass criteria:**
- All 10 FSM states are reachable and correctly reported in `els_state` field.
- Physical buttons (GP15 engage, GP16 feed-hold) produce identical transitions to JSON commands.
- E-stop (GP14) from any engaged state → `ELS_FAULT_ESTOP` immediately; UI shows fault.

---

## Step 10 — X-axis PIO SM + taper

**Setup:** X stepper wired to GP11 (step), GP12 (dir), GP13 (enable). External stepper driver powered.

**Procedure:**
1. Jog X axis: `{"cmd":"jog", "axis":1, "dir":1}`.
2. Engage taper mode with Z:X ratio of 10:1 (0.1 mm taper per mm of Z travel).
3. Cut a short taper pass; measure with digital calipers.

**Pass criteria:**
- X jog: LA on GP11 shows clean step train matching Z jog profile shape.
- Taper: Z/X position ratio after a 20 mm Z pass = 20:2.0 mm ±0.05 mm.

---

## Step 11 — C-axis (VFD) indexing

**Setup:** GP18/GP19 through 3.3 V→5 V level shifter to VFD step/dir input. VFD powered.

**Procedure:**
1. Home spindle to index (rotate until `index_latched` fires).
2. Send `{"cmd":"index", "angle_tenths":1800}` — command 180°.
3. Observe VFD spindle position.

**Pass criteria:**
- Spindle moves to within ±1 encoder count of 180° (= PPR/2 counts from index).
- VFD step pulses on LA: min pulse width ≥ 5 µs (verified by scope on GP18 with 1 ns/div).
- System stays in `ELS_STATE_INDEXING` at target (hold via motor torque); `c_pos_steps` stabilizes.

---

## Step 12 — Reversal semantics

**Procedure A (threading faults on reversal):**
1. Engage threading at 1.0 mm pitch; spin spindle forward.
2. Reverse spindle direction mid-pass.

**Pass criteria:** State immediately → `ELS_FAULT_REVERSAL`. Z stops cleanly. No spurious steps.

**Procedure B (feed mode tracks reversal):**
1. Engage feed mode on Z axis; spin forward 5 revolutions, then reverse 5.
2. Measure net Z travel.

**Pass criteria:** Net Z travel ≈ 0 mm (spindle walked back, axis tracked it). `z_pos_steps` ≈ 0 at end. No FAULT.

---

## Step 13 — Soft-limit enforcement

**Setup:** Configure `z_soft_max_steps = 500` (≈ 3 mm at 167 steps/mm) via serial config.

**Procedure:**
1. Engage threading; rotate spindle past the limit.

**Pass criteria:**
- State → `ELS_FAULT_SOFT_LIMIT` before the 501st step is emitted.
- Z actual travel (measured) does not exceed 3.0 mm ±0.1 mm.
- No steps emitted after fault.

---

## Step 14 — Fault-path robustness (checklist)

Each item maps to a test case in §3 of [`superdroElsPrd.md`](superdroElsPrd.md):

| # | Edge case | Expected outcome |
|---|---|---|
| §3.1 | Spindle stops mid-thread | Z freezes; resumes with correct phase on restart |
| §3.2 | Spindle reversal | Threading → FAULT; Feed → tracks reversal |
| §3.3 | RPM < 10 | Predictor degrades to emit-on-overflow; no crash |
| §3.4 | 4096 PPR × 3000 RPM for 60 s | Zero dropped edges; count matches generator |
| §3.5 | Encoder dither at standstill | Count stable; no spurious steps |
| §3.6 | Missing index pulse | `ELS_FAULT_INDEX_LOST` within 2 rev |
| §3.7 | Impossible ratio | `ELS_FAULT_BAD_RATIO` on arm |
| §3.8 | Accumulator overflow (pathological ratio) | Rejected at init; no overflow in 1M-edge simulation |
| §3.9 | Core 0 hot functions in SRAM | `nm firmware.elf \| grep -E "els_|spindle_|stepper_" \| grep -v " T "` shows no flash-resident hot symbols |
| §3.10 | Ramp release after long gate | `ELS_FAULT_BACKLOG` fires; no burst |
| §3.11 | PPR % N ≠ 0 | `ELS_FAULT_MULTISTART_PPR` on arm |
| §3.12 | Soft-limit violation | `ELS_FAULT_SOFT_LIMIT`; no over-emission |
