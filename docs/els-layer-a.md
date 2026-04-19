# Layer A — Research brief

> Companion to [`superdroElsPrd.md`](superdroElsPrd.md). Analysis only — no implementation. Numbers are grounded against the existing firmware ([`firmware/pio/quadrature.pio`](../firmware/pio/quadrature.pio), [`firmware/pio/stepper.pio`](../firmware/pio/stepper.pio), [`firmware/src/els.c`](../firmware/src/els.c), [`firmware/src/encoder.c`](../firmware/src/encoder.c), [`firmware/src/stepper.c`](../firmware/src/stepper.c)) rather than assumed fresh.

## 1. Current ELS — what's there and why it isn't enough

The Phase 1 prototype in [`firmware/src/els.c`](../firmware/src/els.c) is a **tracking-error loop**, not a Bresenham scheduler. Per-iteration it does:

1. Read spindle count and stepper position.
2. Compute `z_target = spindle_delta × ratio_num / ratio_den` (64-bit integer math).
3. Compare against actual stepper position → produce `s_error`.
4. If `|s_error| > 50`, **refuse to push** (main.c disengages on this threshold).
5. If `s_error != 0`, push **one step per iteration** at a rate derived from current RPM.

Three properties of this design are load-bearing for the rewrite discussion:

- **Position is not the truth — the running computation is.** `z_target` is recomputed each iteration from `spindle_delta × ratio`, so integer overflow or ratio change would be catastrophic (the code partially mitigates with snapped origins at engage). Compare with the PRD's §2.4 "Bresenham accumulator is source of truth for cumulative position" — that's a structural change.
- **One step per loop iteration caps step rate at loop frequency (~50 kHz).** At 125 MHz, with the stepper driver's 2.5 µs minimum pulse, the PIO can emit steps much faster than the loop can feed single steps. This is the mechanism that would cause "falls behind on aggressive threading ratios," which is what the Bresenham + multi-step feeder in the rewrite fixes.
- **No index handling at all.** [`firmware/src/pins.h`](../firmware/src/pins.h) defines `PIN_SPINDLE_INDEX = GP4`, but neither [`firmware/pio/quadrature.pio`](../firmware/pio/quadrature.pio) nor [`firmware/src/encoder.c`](../firmware/src/encoder.c) reads it. Thread-start sync (§2.7) has no hardware path today.

The rewrite is justified. We retain the `els_*` symbol surface (§0.1 of the PRD) but not the algorithm.

---

## 2. Sync model trade-offs

Three candidate architectures, scored against the PRD's non-functional requirements (zero cumulative pitch error, ±10% step interval variance, fail-safe on backlog, deterministic Core 0).

### A. Pulse-accurate Bresenham (emit-on-overflow, no predictor)

Accumulator `acc += ratio_num` on each spindle edge; on overflow, subtract `ratio_den` and emit one step **immediately** into the stepper TX FIFO with `delay = 0` (minimum period).

- **Position accuracy:** exact. Accumulator is integer; no FP drift. This is the baseline for §2.4.
- **Timing smoothness:** poor at non-unit ratios. Example: at ratio 3/7 the step pattern within a revolution is `001011011010110` — each step lands on whichever edge happened to cause overflow. Interval variance is ±(one spindle-edge-period) ≈ tens of µs at mid-RPM. **Fails the ±10% step-variance requirement** for most ratios.
- **Core load:** lowest. One integer add + one compare per spindle edge.
- **Verdict:** correctness floor, not a shippable endpoint. The rewrite's Bresenham core sits at this level; the predictor sits on top.

### B. Rate-matched DPLL (continuous rate × ratio)

Estimate spindle angular velocity with a tracking filter; output steps at `rate × ratio` via a free-running phase accumulator.

- **Position accuracy:** position decouples from spindle rotation. Over long runs, the integrator inside the filter accumulates quantization error → measurable drift over thousands of revolutions. **Fails "zero cumulative pitch error over arbitrarily long runs."**
- **Timing smoothness:** excellent in steady-state.
- **Accel response:** filter lags spindle accel by the filter's time constant. On reversal or rapid spindle decel the phase slips — unacceptable for threading.
- **Verdict:** rejected for threading/feed. Would be fine for a tachometer display, which we don't need.

### C. Hybrid (Bresenham for position, predictor for step timing) — chosen

The Bresenham accumulator from (A) decides *whether* a step is due. A rate predictor decides *when* within the current spindle pulse it should emit. The stepper PIO TX FIFO is fed `(delay_from_now, direction)` tuples; the PIO emits the pulse edge at exactly that delay.

Concretely:
- Accumulator state is the single source of truth for cumulative position (preserves §2.4).
- Predictor uses the last-known spindle pulse period to estimate the fraction of the current period at which overflow occurred, then converts that to PIO ticks and pushes it as the "delay before emitting this step."
- If the predictor is wrong (spindle accelerated/decelerated), the next overflow's prediction self-corrects; the accumulator doesn't care because it's advanced by actual spindle edges, not predicted ones.
- At very low RPM (<10 RPM, §3.3), the predictor's horizon exceeds one PIO-timer period and it gracefully degrades to "delay 0, emit immediately" — i.e. falls back to model (A).

- **Position accuracy:** identical to (A) — predictor errors don't accumulate.
- **Timing smoothness:** within ±(one quadrature-edge period / 4) at steady-state, which at 4096 PPR × 3000 RPM is ~1.2 µs. Comfortably inside the ±10% of a typical threading step interval.
- **Core load:** slightly higher than (A): one divide (predictor lookahead) per emitted step, not per edge. Accumulator update is one add per edge regardless.
- **Matches PIO TX contract:** our [`firmware/pio/stepper.pio`](../firmware/pio/stepper.pio) already consumes a period value per step. The predictor's output is exactly that format.

**This is the PRD's mandated model. Layer A confirms it's the right choice on RP2040.**

---

## 3. RP2040 @ 125 MHz risks

Numbers are computed for worst-case **4096 PPR × 3000 RPM**, which is the ceiling set in PRD §0:

- Shaft pulses/sec: `4096 × 3000 / 60 = 204,800`
- 4× decoded edges/sec: `819,200`
- Period per edge: `1.22 µs` ≈ `152 cycles` @ 125 MHz

### 3.1 PIO decoder headroom — good

The existing [`firmware/pio/quadrature.pio`](../firmware/pio/quadrature.pio) is a 5-instruction continuous sample loop (`mov isr,null` → `in pins,2` → `mov x,isr` → `jmp x!=y,changed` → `jmp done` / `push noblock` + `mov y,x`). At 125 MHz and `clkdiv=1.0` it samples at **25 MHz effective**, i.e. one state sample every 40 ns.

- Minimum edge-to-edge separation the PIO can resolve: 40 ns.
- Actual minimum edge separation at worst case: 1.22 µs (30× margin).
- PIO decode margin vs. encoder edge rate: **~30×**. Well above the 2× bar the PRD sets (§0, §6). No clock bump is required.

**Extension for the index pulse** (PRD §2.1, new spindle decoder): sample 3 pins `[A, B, I]` instead of 2. Push the full 3-bit state on any change. In-program edits: the `in pins, 2` becomes `in pins, 3`; the `jmp x!=y` comparison still holds; the LUT in C expands from 4×4 to 8×8 with most entries being "index edge with no A/B change" (→ emit an index event, delta 0). One extra PIO instruction at most. Still inside headroom.

### 3.2 PIO → Core 0 FIFO drain — **the real bottleneck**

The RX FIFO on a PIO SM is 4 words deep (8 if joined TX+RX, but that costs us the ability to push data back for in-band configuration — we don't use TX on the decoder SM, so joining is free here).

- Worst-case events/sec into the FIFO: `819,200`.
- If Core 0 drains at the current 50 kHz loop rate: `819,200 / 50,000 = 16.4 events per iteration`. FIFO depth 4 (or 8 joined) **overflows**. Silent data loss — unacceptable for "zero cumulative pitch error."
- If Core 0 drains free-running (no `sleep_us(20)` at bottom of [`main.c`](../firmware/src/main.c:117)): achievable drain rate is bounded by the Bresenham update cost per event (≈ 20–40 cycles), so ~3–6 M events/sec drainable. **Sufficient** — but requires removing the sleep and proving nothing else in the hot loop stalls.

**Two mitigations, pick one in Layer B:**

1. **DMA ring (recommended).** A single RP2040 DMA channel pulls one word per DREQ from the PIO RX FIFO into an SRAM ring buffer. Core 0 reads from the ring at its own pace; FIFO never overflows because DMA keeps it drained at PIO rate. Cost: one DMA channel (we have 12), a ~256-word ring (1 KB SRAM), one init-time hookup. Also decouples spindle data capture from Core 0 jitter completely.
2. **Free-run Core 0 + join FIFO to 8 deep.** No DMA. Relies on Core 0 never stalling for >8 edge periods (~10 µs worst case). Cheaper, but fragile: any hot-path function accidentally left in flash could trigger a XIP stall and drop events.

Recommendation: **DMA ring**. It removes an entire class of latency-coupled failure from the design and matches the "deterministic by construction" posture of §2.2.

### 3.3 Flash-XIP stalls on Core 0 — mitigable by construction

The RP2040 has no data cache; instruction prefetch from flash via XIP has variable latency depending on cache line residency. A flash-resident hot-path function can stall Core 0 for 100–300 ns on a cold fetch, plus arbitrary additional time if the QSPI controller is busy (e.g. during a Core 1 flash write for config persistence).

Mitigation is mandated by PRD §2.2: mark all Core 0 hot-path functions `__not_in_flash_func(...)`. Layer B must specify which functions. Non-exhaustive list from a correct implementation: spindle FIFO / DMA-ring drain, Bresenham update, predictor compute, stepper-FIFO feed, mode-FSM step, safety check, button edge detect. The ramp generator for jog mode is also hot.

Config persistence ([`firmware/src/config.c`](../firmware/src/config.c)) must never write to flash with Core 0 in the middle of an engaged ELS mode — document this as an invariant in Layer B.

### 3.4 SIO / spinlock contention between cores

Core 0 writes `status_snapshot_t` and Core 1 reads it (see [`firmware/src/main.c:96`](../firmware/src/main.c#L96)). Current code uses a plain `volatile` — acceptable at 50 Hz read rate but not defensible with a dozen fields some of which are int64.

**Recommended:** double-buffer snapshot with an atomic pointer swap. Core 0 writes into the "back" buffer and performs a single aligned 32-bit pointer store to publish; Core 1 reads the published pointer (aligned load) and dereferences. No spinlock; no torn reads. This is the idiomatic RP2040 pattern for cross-core status.

Avoid spinlocks in Core 0's hot path unconditionally — `spin_lock_blocking` can stall Core 0 indefinitely if Core 1 holds a lock during a USB stall.

### 3.5 Stepper PIO TX FIFO underrun

At steady-state, Core 0 feeds one step per Bresenham overflow. TX FIFO depth 4 means Core 0 has up to 4-step headroom against its own jitter. At 200 kHz max step rate that's 20 µs of slack — comparable to typical Core 0 loop jitter.

Not an issue at realistic threading ratios. At jog mode's peak step rate (driver max, likely ≤200 kHz for a CL57T), still fine.

If Layer A finds a corner case where the feeder undershoots (e.g. very high ratio pushing peak instantaneous rate above steady-state), DMA-feed the stepper PIO as well. Document as an escalation path, not a day-one design choice.

### 3.6 Index pulse integrity

PRD §3.6: missing index pulse must FAULT rather than silently re-phase. Mechanism: timestamp each index event (in PIO ticks at arrival) and compute inter-index interval. If the observed interval deviates from the expected `60 / RPM` by more than a tolerance (start with ±10%), raise a fault. This runs in the decoder consumer on Core 0.

Glitch rejection: require the index GPIO to be high for ≥N consecutive PIO samples before accepting the edge. At 25 MHz sample rate, N=3 gives 120 ns rejection. Physical optical index pulses are typically ≥10 µs wide so this has plenty of margin.

---

## 4. Prototype-first bring-up ordering

Each step must produce a scope or LA trace that passes before the next step is layered on. Any failure mode requires a root-cause, not a workaround.

1. **Extend [`firmware/pio/quadrature.pio`](../firmware/pio/quadrature.pio) to sample `[A, B, I]`.** Target: X/Z scale decoders continue to work unchanged on their 2-pin instantiations (the PIO program handles both cases by initialization, or we fork the program). Pass: no regression in existing scale-count tests ([`firmware/test/test_encoder.c`](../firmware/test/test_encoder.c)).
2. **Wire spindle SM to DMA ring.** Spin encoder at max rated speed with a signal generator or pin toggler. Pass: zero dropped events over a 1-hour run; ring high-water-mark monitored.
3. **Index capture and timestamping.** Feed a simulated 1-pulse-per-rev index alongside quadrature. Pass: inter-index interval reported matches input to within one quadrature-edge period.
4. **Bresenham + predictor unit tests (host, no hardware).** Under [`firmware/test/`](../firmware/test/) following the existing mock pattern. Pass: 1M-spindle-edge simulation shows zero cumulative position error at arbitrary rational ratios; step interval variance histogram stays inside ±10% at steady rate.
5. **Stepper FIFO feeder in pure-Bresenham mode** (predictor bypassed, `delay=0`). Drive existing Z stepper. Pass: physical Z travel measured against spindle rotation matches ratio exactly over 1000 revolutions.
6. **Enable predictor.** Pass: logic-analyzer capture of step pulses shows ±10% interval variance at steady RPM.
7. **Ramp generator in isolation (JOG mode, no spindle input).** Pass: trapezoidal velocity profile visible on LA; no missed steps on accel/decel.
8. **Mode FSM skeleton + JSON protocol integration.** Use existing [`firmware/src/protocol.c`](../firmware/src/protocol.c) transport. Commands: `arm`, `engage`, `disengage`, `feed-hold`, `resume`, `reset-fault`. Physical buttons (GP15/16/17) enter the same FSM. Pass: Android/webapp can drive the FSM through all states.
9. **Thread-start synchronization (single-start).** First step after `THREADING_ARMED` waits for index. Pass: 10 consecutive engage cycles produce threads that land on the same angular position of the workpiece within one encoder edge.
10. **Multi-start offset math (`k·PPR/N` wait after index).** Pass: 2-start thread cut, verified visually and by measurement. Reject `PPR % N != 0` configurations at setup.
11. **X-axis PIO SM + dual-axis engagement (taper).** Pass: cone cut with Z:X ratio of choice, measured taper angle matches spec.
12. **C-axis (VFD) indexing.** Requires VFD pin assignments answered in §5 below. Pass: rotate spindle to programmed angle and hold within ±1 encoder edge.
13. **Reversal semantics (§2.6).** Threading mode: mid-pass reversal raises FAULT, stepper disengages cleanly. Feed mode: reversal tracks, Z walks back in lockstep. Pass: both behaviors captured on LA.
14. **Fault-path rigor (§3.10, §2.9, §3.11, §3.12).** Each named edge case in §3 of the PRD has a dedicated test.

Steps 1–7 are hardware-validatable against existing SuperDRO wiring. Steps 8–14 require the new X and C wiring; those are gated by §5 answers.

---

## 5. Resolved inputs (from operator, 2026-04-19)

Layer B binds to these values. If any of them changes, Layer B must be revisited before Layer C proceeds.

| # | Item | Value |
|---|---|---|
| 1 | X stepper pins | GP11=step, GP12=dir, GP13=enable (existing `pins.h` reservation, confirmed) |
| 2 | C-axis (VFD) pins | GP18=step, GP19=dir. No VFD fault-input wired (deferred) |
| 3 | VFD electrical | 5 V opto-input, ≥5 µs pulse width. External 3.3 V→5 V level shift required — document the buffer circuit in the wiring BOM |
| 4 | Soft-limit behavior | FAULT on the step that would cross; no pre-fault auto-stop, no early-warning event |
| 5 | Z max step rate | 200 kHz (matches existing cap in [`els.c:44`](../firmware/src/els.c#L44)) |
| 6 | X max step rate | 200 kHz (symmetric with Z) |
| 7 | VFD max rate | 10 kHz (typical opto-input ceiling) |
| 8 | Config schema | Extend `config_t` in place; missing-field defaults on load from flash. No schema-version bump |

**Implications for Layer B:**

- **PIO pulse width for C-axis (PIO1 SM2).** At 125 MHz, a 5 µs minimum pulse is 625 cycles. The existing [`firmware/pio/stepper.pio`](../firmware/pio/stepper.pio) uses the `Y` register loaded at init; the C-axis SM instantiation uses a different `Y` value than Z/X. Layer B's stepper-module header must expose the pulse-width constant per-axis, not as a global.
- **VFD max rate constrains ramp + ratio validation.** C-axis commanded step rate is capped at 10 kHz at config-validation time. Indexing moves are slow by design; document expected seek time (worst case ~1s for 360°).
- **Config struct addition is non-breaking.** A migration-free path: when `config_load` reads a blob smaller than the current `config_t`, zero-fill the tail and apply field-level defaults (e.g. `ramp_backlog_threshold = 16`, soft limits = ±1000 mm, max rates from table above). Document this as the canonical "read old config" path.
- **VFD fault input is out-of-scope for now.** Layer B reserves the concept (a `c_axis_fault` flag in the status snapshot) but does not wire a GPIO. A future PRD adds the input when the VFD's feedback behavior is characterized.
- **No fault-output LED.** The existing onboard CYW43 LED is sufficient for the FAULT indicator; Layer B blinks it via [`firmware/src/safety.c`](../firmware/src/safety.c)'s existing `safety_led_update()` path.

**Layer B is unblocked.**

---

## 6. Summary recommendations

- **Model:** Hybrid (Bresenham + rate predictor). Confirmed as correct choice against RP2040 constraints.
- **Clock:** 125 MHz stock. ~30× PIO decode margin at worst case; no bump needed.
- **Spindle FIFO drain:** DMA ring buffer, not free-run polling. Removes a whole class of latency bugs.
- **Index capture:** Extend [`firmware/pio/quadrature.pio`](../firmware/pio/quadrature.pio) to 3-pin sampling with tagged events, not GPIO IRQ. Keeps index ordering synchronous to quadrature.
- **Cross-core handoff:** Double-buffered snapshot + atomic pointer swap. No spinlocks on Core 0.
- **Hot-path residency:** `__not_in_flash_func` on every Core 0 hot function; Layer B must enumerate them.
- **Bring-up:** 14-step ordered ladder; each step has a pass-criterion on scope/LA or host unit test before the next layer is added.

Layer B may proceed once §5 answers are in hand.
