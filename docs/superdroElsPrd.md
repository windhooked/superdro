# SuperDRO — Full ELS Subsystem Specification

> **Prompt usage:** Paste this entire document as the opening message of a coding-agent session inside the `superdro` repo. The agent is expected to read the existing codebase to align with repo conventions (naming, file layout, Makefile/CMake style, PIO build integration) before producing code.

---

## 0. Context & constraints

SuperDRO is a Raspberry Pi Pico W–based digital readout + electronic leadscrew controller for a metalworking lathe. The firmware is **mixed PIO assembly + C** using the Pico SDK. The operator UI lives on an Android tablet connected over USB-CDC JSON; the Pico itself has no local display.

You are extending SuperDRO with a **full Electronic Leadscrew (ELS)** subsystem. All other SuperDRO features (glass-scale decoding, DRO position reporting, JSON protocol, physical buttons, config/NVM, safety interlocks) already exist — integrate, don't duplicate. Before writing code, enumerate the existing modules you will interact with and confirm their public APIs.

### 0.1 Current state (important)

A Phase 1 threading-only ELS prototype already exists in [`firmware/src/els.c`](../firmware/src/els.c) / [`els.h`](../firmware/src/els.h): single Z axis, three states (`ELS_IDLE` / `ELS_ENGAGED` / `ELS_FEED_HOLD`), pitch-only. **This PRD replaces it with a full multi-axis FSM.** Retain the existing public entry points (`els_init`, `els_engage`, `els_disengage`, `els_update`, `els_get_state`, etc.) as a compatibility shim so [`main.c`](../firmware/src/main.c) and [`protocol.c`](../firmware/src/protocol.c) continue to build; the internals are a full rewrite. Add new entry points for feed, taper, jog, indexing, and C-axis as required.

### Hardware target
- MCU: RP2040 on Pico W (dual Cortex-M0+, 2× PIO blocks × 4 SMs, 264 KB SRAM) + CYW43439 radio for onboard LED only.
- System clock: **125 MHz** (stock). Do not raise the system clock in Layer C; any bump must come back through a separate PRD after Layer A proves the PIO decoder is within 2× headroom at worst case.
- Spindle input: incremental rotary encoder, **quadrature + index (Z) pulse**, TTL/5V level-shifted to 3.3V. PPR range 600–4096 (configurable via `config.c`).
- Spindle drive: **VFD with step/dir or PWM input** — the spindle is firmware-controllable, so true C-axis indexing is in scope.
- Axis outputs: **3 axes** (Z, X, C/spindle-drive), each **Step + Direction** at 3.3V, wired to external stepper/servo drivers (Z and X) and to the VFD (C). No Enable line required from the ELS core (handled by existing DRO I/O).
- No external discrete logic. All timing, decoding, and pulse generation run on-chip (PIO + cores).

### Functional scope (must implement all)
1. **Threading** — Z slaved to spindle at an exact rational ratio; metric, imperial, and module pitches; multi-start threads.
2. **Feed** — Z and/or X at a programmable rate per spindle revolution; independent of thread tables.
3. **Jog** — manual motion of any axis at ramped rates, spindle-independent.
4. **Taper** — Z and X simultaneously slaved to spindle with independent ratios; any combination yields a conical surface.
5. **Indexing / C-axis** — rotate the VFD-driven spindle to an absolute angle and hold; uses the index pulse as the zero reference.
6. **Spindle reversal** — see §2.6 for per-mode semantics. **Threading faults on reversal**; feed and taper track through reversal in lockstep without losing phase.

### Non-functional requirements
- **Zero cumulative pitch error** over arbitrarily long runs. Pitch must be exact to the rational ratio.
- **Step timing smoothness:** step interval variance within ±10% of nominal at steady spindle RPM, measured on a logic analyzer.
- **Real-time safety:** spindle-stop-mid-thread must not drift, overshoot, or emit spurious steps.
- **Fail-safe:** if the step rate would exceed a per-axis configured maximum, or the Bresenham backlog exceeds the configured threshold during a ramp gate, ELS must **FAULT** and disengage rather than miss or burst steps silently (same principle as clough42 ELS v1.4).
- **Deterministic Core 0 loop:** no heap allocation, no blocking calls, no stdio from Core 0's hot path after boot.

---

## 1. Deliverable layering

Produce the output in three layers, clearly separated. **Do not skip layers.** Each layer must be internally consistent before the next is written.

### Layer A — Research brief
Short document (no code). Covers:
- Trade-off analysis for the three sync models (pulse-accurate Bresenham, rate-matched DPLL, hybrid) and justification for choosing hybrid.
- Risks specific to RP2040 @ 125 MHz: FIFO depth, ISR latency vs. PIO-driven timing, flash-XIP stalls on Core 0, SMP cache considerations (there is no data cache, but SIO/spinlock contention matters).
- **PIO decoder headroom calculation** at worst-case 4096 PPR × 3000 RPM ≈ 3.3 Mevents/s post-4× decode. If margin vs. 125 MHz is <2×, surface it explicitly rather than silently asking for a clock bump.
- Prototype-first ordering: what to bring up and validate on a scope/LA before layering the next piece.
- Open questions the human must answer before implementation freezes (see §7 — should be a short list now that most are resolved).

### Layer B — Architecture
Module-level design, no function bodies. Covers:
- Module boundaries and responsibilities (including the `els.c` compatibility shim over the new engine — see §0.1).
- PIO state-machine allocation across PIO0/PIO1 (8 SMs total — see §2.1 for the fixed table).
- Core 0 vs. Core 1 responsibility split (see §2.2 — fixed).
- Inter-core communication primitives (SPSC ring, FIFO, spinlock) and their sizing.
- Public C headers (types, function signatures, invariants) for each module. No implementations.
- State machine diagrams (ASCII or Mermaid) for: overall ELS mode FSM, per-axis engagement FSM, indexing controller, thread-start/multi-start synchronization.

### Layer C — Implementation
Code and code-equivalent artifacts:
- PIO `.pio` source for the spindle quadrature+index decoder (extension of the existing [`firmware/pio/quadrature.pio`](../firmware/pio/quadrature.pio)) and the per-axis step-gen program (extension of [`firmware/pio/stepper.pio`](../firmware/pio/stepper.pio)).
- C source and headers for all modules defined in Layer B.
- Unit tests (host-runnable under [`firmware/test/`](../firmware/test/) — the Bresenham core, ramp generator, FSM transitions, and ratio reducer must all be testable without hardware; follow the existing mock pattern).
- Build integration (`CMakeLists.txt` fragments consistent with the existing `superdro_quadrature_pio_h` / `superdro_stepper_pio_h` targets).
- On-target bring-up procedure: exact steps, expected scope traces, pass/fail criteria per step.

---

## 2. Required architecture (constraints on Layer B)

The following decisions are **fixed**. Do not relitigate them in the research brief except to flag an RP2040-specific blocker.

### 2.1 PIO allocation

This table reflects the existing repo and the ELS extensions on top of it. PIO0 SM1/SM2 continue to host the X and Z glass-scale decoders; the spindle decoder on PIO0 SM0 is extended to also latch the index pulse.

| PIO | SM | Function | Status |
|---|---|---|---|
| PIO0 | 0 | Spindle quadrature **+ index** decoder; emits signed delta events and index latch | Extend existing |
| PIO0 | 1 | X-axis glass-scale quadrature decoder | Existing, keep |
| PIO0 | 2 | Z-axis glass-scale quadrature decoder | Existing, keep |
| PIO0 | 3 | Reserved | — |
| PIO1 | 0 | Z-axis step/dir generator | Existing, keep |
| PIO1 | 1 | X-axis step/dir generator | New |
| PIO1 | 2 | C-axis (spindle VFD step/dir) generator | New |
| PIO1 | 3 | Reserved | — |

### 2.2 Core split
- **Core 0:** hard real-time. Spindle decoder consumer, per-axis Bresenham advance, rate predictor, ramp generator, PIO TX FIFO feeder, safety checks, physical-button edge detect, status snapshot writer. **Runs from SRAM only.** Any function called from Core 0's hot path must be marked `__not_in_flash_func(...)` so it is copied into SRAM at boot.
- **Core 1:** USB-CDC JSON protocol (rx parsing, status tx), LED updates, non-real-time housekeeping. May stall on flash XIP.

This matches [`firmware/src/main.c`](../firmware/src/main.c): Core 0 already runs the ~50 kHz control loop and Core 1 runs `protocol_*` at ~50 Hz. Do not swap them.

### 2.3 Step generation model
Step pulse edges must be emitted **by the PIO state machine**, not by Core 0. Core 0 computes "delay until next step" in PIO clock ticks and pushes `(delay, direction)` into the SM's TX FIFO. Queue depth ≥ 4 steps per axis; if DMA is used to extend queue depth, document why. Follow the pattern established in [`firmware/src/stepper.c`](../firmware/src/stepper.c) / [`firmware/pio/stepper.pio`](../firmware/pio/stepper.pio).

### 2.4 Divider algorithm
Signed-accumulator Bresenham / DDA per axis, with a rate-prediction overlay for smoothness:
- Accumulator advances on every spindle delta event.
- On overflow (positive or negative), a step is scheduled at a **predicted PIO tick**, not immediately. The predictor uses the last-known spindle rate and the fractional phase of the overflow within the current spindle pulse.
- The Bresenham accumulator is the **source of truth** for cumulative position. The predictor only smooths timing; prediction error never accumulates into position error.

### 2.5 Ramp generator
Trapezoidal velocity profile, applied as a floor on step interval (`effective_delay = max(bresenham_predicted_delay, ramp_min_delay)`). The ramp:
- Drives alone in jog mode (no spindle input).
- Gates engagement transitions (threading/feed start, stop, mode change).
- Is idle and transparent once steady-state Bresenham rate is below the ramp limit.
- **Never caps the Bresenham accumulator** — the accumulator remains the sole source of truth for position (§2.4).
- **Monitors backlog instead.** If the queued-but-not-emitted step count exceeds the configured per-axis `ramp_backlog_threshold`, transition to `FAULT` and disengage. The threshold is the guardrail against "ramp release emits a burst" and against "axis physically cannot keep up with spindle rate."

### 2.6 Mode FSM

States: `IDLE`, `THREADING_ARMED`, `THREADING_ENGAGED`, `FEED_ENGAGED`, `JOG`, `TAPER_ENGAGED`, `INDEXING`, `FAULT`.

Transitions must be total and documented. `FAULT` is sticky and requires an explicit user reset via the JSON protocol.

**Spindle reversal semantics (per state):**
- `THREADING_ARMED`, `THREADING_ENGAGED`: reversal detected → **FAULT**. Operator must retract and re-arm. Matches clough42 v1.4 and prevents accidental recut on the return pass.
- `FEED_ENGAGED`, `TAPER_ENGAGED`: reversal tracks — accumulators walk back, direction bits flip atomically per axis, no loss of phase. Intended for conventional turning passes.
- `JOG`, `INDEXING`, `IDLE`, `FAULT`: spindle direction is not slaved; reversal is ignored (but index-pulse interval sanity check still runs in `INDEXING`).

### 2.7 Thread-start synchronization (single and multi-start)

In `THREADING_ARMED`, the first step waits for the next spindle index pulse. For an **N-start thread on pass k ∈ {0 … N−1}**, after the index pulse is latched, wait an additional `k · PPR / N` quadrature-decoded spindle pulses before releasing the first step. This guarantees each start begins at its correct rotational phase.

Requirements:
- `PPR` must be divisible by `N`; if not, reject the thread configuration at setup time.
- The offset counter runs in the spindle decoder consumer on Core 0, not in the PIO SM.
- The latch (`index_seen` + `offset_remaining == 0`) is the sole release condition; no wall-clock timers.
- Multi-start passes must be repeatable across a full job — losing the index fault-drops the axis (§3.6); passes never silently re-phase.

### 2.8 Ratio representation
Ratios are stored as reduced rationals `(numerator, denominator)` of `int64`. Reduce to lowest terms (GCD) at configuration time. Validate that no single-pulse step emission exceeds the axis max rate.

### 2.9 Soft limits
ELS enforces per-axis soft limits using the existing `config.c` axis min/max fields (extend the config schema if fields are missing). Before emitting a step that would cross an enforced limit, ELS raises `FAULT`. Limits are checked in the Core 0 hot path; the check must be branch-light and non-allocating. The DRO/UI layer may perform a pre-engage feasibility check (does starting position + planned travel fit?) but is not the enforcement boundary — ELS is.

---

## 3. Edge cases the implementation must handle

Each must have a named test or documented hardware bring-up check:
1. Spindle stops mid-thread — all axes freeze cleanly, no drift, resumes on spindle restart with correct phase.
2. Spindle reversal under load — behavior per §2.6: FAULT in threading; accumulators walk back and direction bits flip atomically in feed/taper.
3. Very low spindle RPM (< 10 RPM) — predictor delay exceeds comfortable bound; system degrades gracefully to "emit on overflow, accept jitter."
4. Very high spindle RPM near encoder edge-rate budget — PIO decoder must not drop counts; verified on logic analyzer under sustained 4096 PPR × 3000 RPM.
5. Encoder dither at standstill — direction chatter suppressed (deadband or N-pulse debounce).
6. Index pulse glitch / missing pulse — detected against expected pulse interval; FAULT rather than silently re-phase (this is what makes multi-start starts repeatable across a job).
7. Ratio input producing impossible output rate — rejected at configure time with a clear error.
8. Accumulator overflow — int64 headroom documented; pathological ratios rejected.
9. Core 0 starvation (long flash stall leaking into hot path) — must not happen by construction; prove it via SRAM residency of hot functions (`__not_in_flash_func`).
10. Ramp release after long gate — backlog counter exceeds `ramp_backlog_threshold` before any burst can emit → FAULT (§2.5).
11. Multi-start misconfiguration — `PPR % N != 0` rejected at setup (§2.7).
12. Soft-limit violation — commanded step would cross a configured axis min/max → FAULT (§2.9).

---

## 4. Interfaces to existing SuperDRO

Before writing Layer B, **read these modules and document their touch points**:

- **Config / NVM** — [`firmware/src/config.c`](../firmware/src/config.c) / [`config.h`](../firmware/src/config.h). Persist ELS config: thread tables, per-axis ratios, max step rates, `ramp_backlog_threshold`, **axis soft-limit min/max (§2.9 — add these fields if missing).**
- **JSON protocol (primary UI boundary)** — [`firmware/src/protocol.c`](../firmware/src/protocol.c) / [`protocol.h`](../firmware/src/protocol.h). The Android tablet and Go webapp are the only UIs. ELS mode FSM state and commands (arm, engage, disengage, feed-hold, resume, set-pitch, set-ratio, jog, index-to, reset-fault) are exposed exclusively through the existing `status_snapshot_t` tx path and the rx command parser. Extend both as needed; do **not** invent a separate transport.
- **Physical buttons (parallel UI path)** — GP15 (engage), GP16 (feed hold), GP17 (cycle start). `main.c` already edge-detects these. The new ELS FSM must honor them in parallel with JSON commands; both paths enter the same transition table. Document which modes each button is live in (e.g. engage button has different meaning in `IDLE` vs `THREADING_ARMED`).
- **DRO position reporting** — [`firmware/src/encoder.c`](../firmware/src/encoder.c) already publishes X and Z glass-scale positions. When an axis is slaved (threading/feed/taper), the ELS step count must **also** feed the DRO position so the tablet shows consistent numbers; decide in Layer B whether this is by hooking the encoder module or by a new shared position struct. Do not duplicate axis position tracking.
- **Safety / watchdog** — [`firmware/src/safety.c`](../firmware/src/safety.c) / `safety.h`. E-stop (GP14) already drops ELS into `IDLE` via `els_disengage()` in `main.c`; preserve this behavior. Route new ELS faults through the same watchdog / alarm surface (`safety_alarm_active()`) so the UI already sees them.
- **Logging / telemetry** — send ELS faults and diagnostics as new fields in `status_snapshot_t` and as `event` records on the JSON protocol. No new transport.

If any of the above do not exist as described, flag them in the research brief as dependencies rather than silently inventing them.

---

## 5. Quality bar

- Every public function has a doc comment stating: what it does, which core/context it may be called from, whether it is ISR-safe, and any invariants on arguments.
- No magic numbers in the hot path. Timing constants are named and derived from configured hardware parameters.
- PIO programs have cycle-by-cycle comments justifying each instruction's role.
- Unit tests for pure logic (Bresenham, ramp, FSM transitions, ratio reduction, multi-start offset math, soft-limit check) run on host without hardware, under [`firmware/test/`](../firmware/test/).
- Hardware bring-up doc includes expected logic-analyzer traces, not just "it should work."

---

## 6. Out of scope (do not implement)

- G-code parsing. ELS is a motion primitive; G-code can be added later as a Core 1 consumer.
- Closed-loop servo following-error handling beyond step counting.
- Network/remote control.
- Any change to the existing DRO scale input pipeline beyond consuming ELS step counts for slaved-axis position.
- System clock bump above 125 MHz. If Layer A proves <2× PIO decode margin, raise it as a separate PRD.

---

## 7. Resolved inputs (locked for Layer B)

All originally-open questions are answered. Layer B binds to these values; see [`els-layer-a.md`](els-layer-a.md) §5 for the full table and implications.

| # | Item | Value |
|---|---|---|
| 1 | X stepper pins | GP11=step, GP12=dir, GP13=enable |
| 2 | C-axis (VFD) pins | GP18=step, GP19=dir (no VFD fault-input wired) |
| 3 | VFD electrical | 5 V opto, ≥5 µs pulse width; external 3.3→5 V level shift |
| 4 | Soft-limit behavior | FAULT on crossing; no auto-stop, no early-warning event |
| 5 | Z max step rate | 200 kHz |
| 6 | X max step rate | 200 kHz |
| 7 | VFD / C-axis max rate | 10 kHz |
| 8 | Config schema | Extend `config_t` in place; zero-fill tail + field-level defaults on load |

Any revision to this table requires Layer B to be revisited before Layer C proceeds.

---

## 8. Output format

Respond with three clearly delimited sections: `# Layer A — Research brief`, `# Layer B — Architecture`, `# Layer C — Implementation`. Use Mermaid or ASCII for diagrams. Code blocks use the repo's existing language tags. If any constraint in this spec conflicts with something already in the repo, **stop and flag it** rather than guessing.
