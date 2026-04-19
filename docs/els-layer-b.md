# Layer B — Architecture

> Companion to [`superdroElsPrd.md`](superdroElsPrd.md) and [`els-layer-a.md`](els-layer-a.md). Module-level design only — no function bodies. All inputs from Layer A §5 are locked. Layer C must not change the public headers defined here without revisiting this document.

---

## B.0 Open questions from Layer B

None. All inputs are resolved. Layer C may proceed immediately after this document.

---

## B.1 Module map

```
firmware/src/
├── pins.h            extended — X stepper + C-axis (VFD) pins added
├── config.h/.c       extended — ELS fields added to config_t; default-on-load
├── encoder.h/.c      unchanged — X/Z glass-scale decode (PIO0 SM1/SM2)
├── spindle.h/.c      NEW — DMA ring consumer, rate estimator, index latch
├── stepper.h/.c      extended — multi-axis (PIO1 SM0/1/2); per-axis API
├── els_engine.h/.c   NEW — Bresenham accumulator + rate predictor, per-axis
├── els_ramp.h/.c     NEW — trapezoidal velocity profile, per-axis
├── els_fsm.h/.c      NEW — mode FSM: 9 states, total transitions, button/JSON paths
├── els.h/.c          REWRITE — public shim: retains old API, routes to new engine/FSM
├── protocol.h/.c     extended — new JSON commands + status fields for ELS
└── safety.h/.c       unchanged (FAULT propagated via existing safety_alarm path)

firmware/pio/
├── quadrature.pio    extended — 3-pin (A, B, I) sampling, tagged index events
└── stepper.pio       unchanged program; C-axis SM instantiated with wider pulse const
```

**Dependency order (deepest first):**
`pins.h` → `config.h` → `spindle.h`, `encoder.h`, `stepper.h` → `els_engine.h`, `els_ramp.h` → `els_fsm.h` → `els.h` → `protocol.h`, `main.c`

---

## B.2 PIO allocation (confirmed from PRD §2.1)

| PIO | SM | Program | Function | Status |
|---|---|---|---|---|
| PIO0 | 0 | `quadrature.pio` (extended) | Spindle quad + index; 3-pin sampling | Extend existing |
| PIO0 | 1 | `quadrature.pio` | X glass-scale (2-pin, unchanged) | Existing |
| PIO0 | 2 | `quadrature.pio` | Z glass-scale (2-pin, unchanged) | Existing |
| PIO0 | 3 | — | Reserved | — |
| PIO1 | 0 | `stepper.pio` | Z step/dir gen | Existing |
| PIO1 | 1 | `stepper.pio` | X step/dir gen | New |
| PIO1 | 2 | `stepper.pio` | C-axis (VFD) step/dir gen | New |
| PIO1 | 3 | — | Reserved | — |

DMA channel allocation: one channel for PIO0 SM0 RX FIFO → spindle ring buffer. All other PIO I/O is CPU-driven (TX FIFO push from Core 0). One DMA channel consumed; 11 remain free.

---

## B.3 Core assignment

| Resource | Core 0 (RT, ~50 kHz loop) | Core 1 (~50 Hz loop) |
|---|---|---|
| Spindle ring drain | `spindle_update()` — drain DMA ring, update rate, latch index | — |
| Bresenham advance | `els_engine_update()` | — |
| Stepper FIFO feed | `stepper_push()` per axis | — |
| FSM step (event check) | `els_fsm_step()` — polls command queue, evaluates guards | — |
| Physical buttons | Edge detect in `main.c`, converted to FSM events | — |
| Status snapshot publish | `els_status_publish()` — atomic pointer swap | — |
| USB/JSON protocol | — | `protocol_process_rx()`, `protocol_send_status()` |
| Config persist (flash) | — (blocked from Core 0 hot path) | On command from JSON rx |
| LED update | — | `safety_led_update()` |

**Flash-XIP ban on Core 0 hot path.** The following functions must be decorated `__not_in_flash_func(NAME)` in their `.c` files:

- `spindle_update`, `spindle_read_count`, `spindle_read_rate`, `spindle_index_latch_clear`
- `els_engine_update`, `els_engine_reset`, `els_engine_push_steps`
- `els_ramp_update`, `els_ramp_engage`, `els_ramp_disengage`
- `els_fsm_step`, `els_fsm_event`
- `stepper_push`, `stepper_stop`, `stepper_set_dir`, `stepper_fifo_free` (all axes)
- `safety_watchdog_feed`, `safety_estop_active`, `safety_debounce_update`

LUT tables used in the spindle decoder (`quad_lut`) must live in SRAM: declare `static const int8_t quad_lut[8][8] __in_flash("rodata")` is wrong; use `static const int8_t quad_lut[8][8]` in a `.c` file compiled with `-mlong-calls`-equivalent, or copy to SRAM at init. Simplest: declare in a `__not_in_flash_func` translation unit — GCC will place it in SRAM if the TU has no flash-resident code.

---

## B.4 Inter-core communication primitives

### B.4.1 Command channel (Core 1 → Core 0)

Use the RP2040 hardware inter-core FIFO (`multicore_fifo_push_blocking` / `multicore_fifo_pop_timeout_us`). Capacity: 8 words per direction.

Commands are packed into a single `uint32_t`:

```
bits [31:24] = opcode  (els_cmd_opcode_t, 8 values max 255)
bits [23:0]  = payload (opcode-specific; see B.7.5)
```

Core 0 polls the FIFO at the top of its hot loop via `multicore_fifo_rvalid()` (non-blocking). Commands larger than 24-bit payload are sent as two consecutive words (opcode word + data word); Core 0 reads both before acting. Multi-word commands must use a spinlock-free convention: Core 1 pushes the pair atomically (no other Core 1 FIFO pushes between them) by holding the inter-core FIFO lock only for the duration of the pair push. In practice, Core 1's protocol loop is single-threaded, so no lock is needed.

### B.4.2 Status channel (Core 0 → Core 1)

Double-buffered `els_status_t`. Layout:

```c
// SRAM-resident; aligned to 4 bytes
static els_status_t g_status_bufs[2];
static volatile els_status_t *g_status_front; // Core 1 reads this pointer
```

Core 0 writes into the "back" buffer (whichever `g_status_front` does not point to), then performs a single aligned 32-bit pointer store to publish. No spinlock. Core 1 dereferences `g_status_front` without a lock — it may read a mix of two consecutive snapshots, which is acceptable for display-rate telemetry. If a field requires strict atomicity (e.g., `fault_code`), it is a single `uint32_t` and aligned — naturally atomic on Cortex-M0+.

### B.4.3 Spindle DMA ring

```
SRAM buffer: uint32_t g_spindle_ring[256]   // 1 KB; 8-byte aligned for DMA
Read head:   uint32_t g_spindle_ring_rhead   // owned by Core 0
Write index: derived from DMA write address  // DMA hardware owns write
```

DMA config: `DREQ_PIO0_RX0`, `DMA_SIZE_32`, ring write-address with size=8 (256-word ring = 1024-byte = 10-bit wrap). DMA is set up in chain-to-self mode to restart automatically. Core 0 computes new events via `rhead != write_idx` and advances `rhead` modulo 256.

---

## B.5 `pins.h` — additions

```c
// X stepper (PIO1, SM1) — existing reservations, now confirmed
#define PIN_X_STEP          11
#define PIN_X_DIR           12
#define PIN_X_ENABLE        13

// C-axis VFD (PIO1, SM2) — replaces 'reserved for jog' annotation
#define PIN_C_STEP          18
#define PIN_C_DIR           19
// PIN_C_ENABLE: VFD does not require enable from firmware; omit.
// External 3.3V→5V level shift required on PIN_C_STEP and PIN_C_DIR.
```

---

## B.6 `quadrature.pio` — spindle decoder extension

The existing 2-pin program on PIO0 SM0 is replaced by a 3-pin variant for spindle use only. PIO0 SM1 and SM2 continue to use the 2-pin program (instantiation parameter).

Proposed in/out format: the SM pushes one 32-bit word per state change:
- bits [2:0] = new {I, B, A} pin state
- bit [31] = 1 if bit[2] (I) changed from previous sample, 0 otherwise

This lets the C consumer distinguish "index edge" from "quadrature edge" in a single branch.

The C-side decoder uses an 8×8 LUT for quadrature transitions (indexed by prev[1:0] and new[1:0]) and a separate index-change path when bit[31] is set.

---

## B.7 Public headers

### B.7.1 `spindle.h`

```c
#ifndef SPINDLE_H
#define SPINDLE_H

#include <stdint.h>
#include <stdbool.h>

// Initialize spindle PIO SM0 (3-pin), DMA ring, and rate estimator.
// Call after pio_add_program. Must be called from Core 0 before els_init.
// Not callable from ISR. Not hot-path.
void spindle_init(void);

// Drain all pending events from the DMA ring.
// Updates count, rate estimate, and index latch.
// Called from Core 0 hot loop. __not_in_flash_func.
// Returns number of quadrature edges processed this call.
uint32_t spindle_update(void);

// Current spindle position in 4x-decoded quadrature counts.
// Signed; wraps at int32 boundaries (handle in consumer).
// Core 0 only. __not_in_flash_func.
int32_t spindle_read_count(void);

// Estimated spindle rate in quadrature counts per PIO cycle.
// Uses exponential moving average over the last 8 edges.
// Returns 0 if spindle is stopped. Core 0 only. __not_in_flash_func.
uint32_t spindle_read_rate_cppc(void);  // counts per PIO cycle (fixed-point Q16.16)

// True if a new index pulse has been latched since the last call to
// spindle_index_latch_clear(). Cleared by the consumer, not by spindle_update.
// Core 0 only. __not_in_flash_func.
bool spindle_index_latched(void);

// Consume the index latch. Must be called by the thread-start sync logic.
// Core 0 only. __not_in_flash_func.
void spindle_index_latch_clear(void);

// Spindle direction: +1 forward, -1 reverse, 0 stopped.
// Updated by spindle_update. Core 0 only. __not_in_flash_func.
int8_t spindle_direction(void);

// True if the last observed inter-index interval deviated from the
// running average by more than SPINDLE_INDEX_FAULT_PCT percent.
// Must be checked by els_fsm_step. Core 0 only. __not_in_flash_func.
bool spindle_index_fault(void);

// Offset counter for multi-start thread sync (§2.7).
// Sets a countdown of `pulses` quadrature edges after the next index latch.
// When countdown reaches zero, spindle_index_latched() returns true.
// PPR must be divisible by starts; caller validates. Core 0 only.
void spindle_arm_start_offset(uint32_t pulses);  // 0 = use index directly

#endif // SPINDLE_H
```

### B.7.2 `stepper.h` — multi-axis extension

```c
#ifndef STEPPER_H
#define STEPPER_H

#include <stdint.h>
#include <stdbool.h>

// Axis identifiers
typedef enum {
    AXIS_Z = 0,
    AXIS_X = 1,
    AXIS_C = 2,
    AXIS_COUNT = 3,
} stepper_axis_t;

// Per-axis hardware config (set once at init; populated from config_t).
typedef struct {
    uint     step_pin;
    uint     dir_pin;
    uint     enable_pin;   // 255 = no enable pin (C-axis)
    PIO      pio;          // always pio1
    uint     sm;           // 0=Z, 1=X, 2=C
    uint32_t pulse_cycles; // PIO cycles for step-pulse high phase
    uint32_t max_rate_hz;  // max steps/sec; enforced in stepper_delay_for_rate
} stepper_axis_cfg_t;

// Initialize all three axes. Loads stepper.pio program into PIO1 once
// (shared program memory) and configures SM0/1/2 with per-axis constants.
// Not ISR-safe. Called from Core 0 before els_init.
void stepper_init(void);

// Enable or disable the driver for a given axis.
// Core 0 only. __not_in_flash_func.
void stepper_enable(stepper_axis_t axis, bool en);

// Set step direction. True = forward.
// Core 0 only. __not_in_flash_func.
void stepper_set_dir(stepper_axis_t axis, bool forward);

// Push one step-delay word to the axis TX FIFO (non-blocking).
// `delay_cycles`: PIO-cycle low-phase duration. Must be >= pulse_cycles + overhead.
// Returns false if FIFO is full (backlog handling is caller's responsibility).
// Core 0 only. __not_in_flash_func.
bool stepper_push(stepper_axis_t axis, uint32_t delay_cycles);

// Number of free TX FIFO slots for the given axis (0..4).
// Core 0 only. __not_in_flash_func.
uint stepper_fifo_free(stepper_axis_t axis);

// Stop the axis: drain FIFO, restart SM. Motor de-energizes if enable is cleared separately.
// Core 0 only. __not_in_flash_func.
void stepper_stop(stepper_axis_t axis);

// Cumulative step count since last stepper_reset_position call.
// Updated by Core 0 each time stepper_push succeeds.
// Core 0 only. __not_in_flash_func.
int32_t stepper_get_position(stepper_axis_t axis);

// Reset position counter to zero (call at engage origin snap).
// Core 0 only. __not_in_flash_func.
void stepper_reset_position(stepper_axis_t axis);

// Compute delay_cycles for a target step rate in Hz.
// Returns UINT32_MAX if rate exceeds axis max_rate_hz (caller must FAULT).
// Pure math; may be called from any context.
uint32_t stepper_delay_for_rate(stepper_axis_t axis, uint32_t steps_per_sec);

#endif // STEPPER_H
```

### B.7.3 `els_engine.h`

```c
#ifndef ELS_ENGINE_H
#define ELS_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "stepper.h"

// Per-axis Bresenham + predictor state. One instance per driven axis.
typedef struct {
    // Ratio (locked at arm time; reduced to lowest terms)
    int64_t  ratio_num;
    int64_t  ratio_den;

    // Bresenham accumulator (source of truth for position)
    int64_t  accumulator;
    int32_t  position;       // steps emitted since reset; feeds DRO

    // Soft limits in steps (from config; set at arm time)
    int32_t  soft_min_steps;
    int32_t  soft_max_steps;

    // Backlog: steps due (by Bresenham) but not yet pushed to FIFO
    int32_t  backlog;
    int32_t  backlog_threshold; // from config; if exceeded, caller must FAULT

    // Rate predictor: exponential moving average of step interval (PIO cycles)
    uint32_t predicted_delay; // current best estimate; updated each spindle edge

    // Axis identity (for stepper_push calls)
    stepper_axis_t axis;
} els_axis_state_t;

// Initialize one axis state from config values. Not hot-path.
// ratio_num/den must already be reduced by gcd. Returns false if ratio is invalid
// (would exceed axis max step rate at max spindle RPM) or soft limits are
// misconfigured.
bool els_engine_axis_init(els_axis_state_t *ax, stepper_axis_t axis,
                          int64_t ratio_num, int64_t ratio_den);

// Reset accumulator and position to zero (call at engage origin snap).
// Core 0. __not_in_flash_func.
void els_engine_axis_reset(els_axis_state_t *ax);

// Advance the Bresenham accumulator by `spindle_delta` counts.
// For each overflow, increments backlog and updates predicted_delay.
// Does not push to FIFO — caller drains backlog via els_engine_axis_flush.
// Returns false if soft_min or soft_max would be crossed (caller must FAULT).
// Core 0. __not_in_flash_func.
bool els_engine_axis_advance(els_axis_state_t *ax, int32_t spindle_delta,
                             uint32_t spindle_rate_cppc);

// Push up to `max_steps` steps from the backlog into the stepper FIFO.
// Uses predicted_delay as the FIFO delay value (already ramp-floored by caller).
// Returns the number of steps pushed. Decrements backlog accordingly.
// Returns -1 if soft limit would be crossed mid-flush (caller must FAULT).
// Core 0. __not_in_flash_func.
int32_t els_engine_axis_flush(els_axis_state_t *ax, uint32_t effective_delay,
                              int32_t max_steps);

// True if backlog > backlog_threshold.
// Core 0. __not_in_flash_func.
bool els_engine_axis_backlog_fault(const els_axis_state_t *ax);

#endif // ELS_ENGINE_H
```

### B.7.4 `els_ramp.h`

```c
#ifndef ELS_RAMP_H
#define ELS_RAMP_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    RAMP_IDLE   = 0,
    RAMP_ACCEL  = 1,
    RAMP_CRUISE = 2,
    RAMP_DECEL  = 3,
} ramp_phase_t;

typedef struct {
    ramp_phase_t phase;
    uint32_t     current_delay; // PIO cycles between steps (large = slow)
    uint32_t     min_delay;     // PIO cycles at peak speed
    uint32_t     max_delay;     // PIO cycles at start/stop (very slow)
    uint32_t     delta;         // delay change per step (linear ramp)
    int32_t      steps_remaining; // for decel: steps until stop (-1 = infinite)
} els_ramp_state_t;

// Initialize ramp from config (max/min step rates for the axis).
// Not hot-path.
void els_ramp_init(els_ramp_state_t *r, uint32_t min_delay, uint32_t max_delay,
                   uint32_t accel_delta);

// Begin acceleration toward cruise (IDLE → ACCEL → CRUISE).
// Core 0. __not_in_flash_func.
void els_ramp_engage(els_ramp_state_t *r);

// Begin deceleration to stop (CRUISE/ACCEL → DECEL → IDLE).
// Core 0. __not_in_flash_func.
void els_ramp_disengage(els_ramp_state_t *r);

// Advance ramp by one step. Returns the effective delay to use for this step.
// When phase == RAMP_IDLE, returns UINT32_MAX (no step should emit).
// Core 0. __not_in_flash_func.
uint32_t els_ramp_step(els_ramp_state_t *r);

// The ramp floor: effective_delay = max(bresenham_delay, ramp_delay).
// Callers use this to blend ramp with Bresenham in threading/feed modes.
// Core 0. __not_in_flash_func.
uint32_t els_ramp_floor(const els_ramp_state_t *r, uint32_t bresenham_delay);

// True if the ramp has fully stopped (phase == RAMP_IDLE after a disengage).
// Core 0. __not_in_flash_func.
bool els_ramp_idle(const els_ramp_state_t *r);

#endif // ELS_RAMP_H
```

### B.7.5 `els_fsm.h`

```c
#ifndef ELS_FSM_H
#define ELS_FSM_H

#include <stdint.h>
#include <stdbool.h>

// -------------------------------------------------------------------------
// States
// -------------------------------------------------------------------------
typedef enum {
    ELS_STATE_IDLE             = 0,
    ELS_STATE_THREADING_ARMED  = 1,  // Waiting for index + offset
    ELS_STATE_THREADING_ENGAGED = 2,
    ELS_STATE_THREADING_HOLD   = 3,  // Feed-hold within a threading pass
    ELS_STATE_FEED_ENGAGED     = 4,  // Z and/or X rate-per-rev feed
    ELS_STATE_JOG              = 5,
    ELS_STATE_TAPER_ENGAGED    = 6,  // Z + X simultaneous with independent ratios
    ELS_STATE_INDEXING         = 7,  // C-axis to absolute angle
    ELS_STATE_FAULT            = 8,  // Sticky; reset via CMD_RESET_FAULT only
} els_state_t;

// -------------------------------------------------------------------------
// Fault codes (stored in status for UI display)
// -------------------------------------------------------------------------
typedef enum {
    ELS_FAULT_NONE            = 0,
    ELS_FAULT_BACKLOG         = 1,  // Step backlog exceeded threshold (§2.5)
    ELS_FAULT_SOFT_LIMIT      = 2,  // Axis crossed soft limit (§2.9)
    ELS_FAULT_RATE_EXCEEDED   = 3,  // Computed step rate > axis max
    ELS_FAULT_REVERSAL        = 4,  // Spindle reversed during threading (§2.6)
    ELS_FAULT_INDEX_LOST      = 5,  // Index pulse interval fault (§3.6)
    ELS_FAULT_ESTOP           = 6,  // E-stop asserted
    ELS_FAULT_BAD_RATIO       = 7,  // Ratio rejected at configure time
    ELS_FAULT_MULTISTART_PPR  = 8,  // PPR not divisible by N (§2.7)
} els_fault_code_t;

// -------------------------------------------------------------------------
// Command opcodes (packed into inter-core FIFO words; see B.4.1)
// -------------------------------------------------------------------------
typedef enum {
    CMD_NOP               = 0x00,
    CMD_ARM_THREADING     = 0x01,  // payload: index into thread table (uint8)
    CMD_START_FEED        = 0x02,  // payload: feed rate (uint16 in µm/rev)
    CMD_START_TAPER       = 0x03,  // payload: (none; ratios set separately)
    CMD_JOG_START         = 0x04,  // payload: axis[7:4] | direction[3:0]
    CMD_JOG_STOP          = 0x05,
    CMD_INDEX_TO          = 0x06,  // payload: target angle (uint16, 0.1° units)
    CMD_DISARM            = 0x07,  // ARMED → IDLE
    CMD_DISENGAGE         = 0x08,  // any ENGAGED → IDLE
    CMD_FEED_HOLD         = 0x09,  // THREADING_ENGAGED → THREADING_HOLD
    CMD_RESUME            = 0x0A,  // THREADING_HOLD → THREADING_ENGAGED
    CMD_RESET_FAULT       = 0x0B,  // FAULT → IDLE (only allowed transition out of FAULT)
    CMD_SET_RATIO         = 0x0C,  // two-word command: [opcode] [num<<32|den — second word]
    CMD_SET_STARTS        = 0x0D,  // payload: N starts (uint8), k offset (uint8)
} els_cmd_opcode_t;

// -------------------------------------------------------------------------
// Status snapshot (Core 0 → Core 1 via double buffer)
// -------------------------------------------------------------------------
typedef struct {
    els_state_t      state;
    els_fault_code_t fault;
    int32_t          z_pos_steps;
    int32_t          x_pos_steps;
    int32_t          c_pos_counts;   // spindle encoder count at last index
    int32_t          spindle_count;
    float            spindle_rpm;
    int32_t          z_backlog;
    int32_t          x_backlog;
    bool             index_latched;
    bool             estop;
} els_status_t;

// -------------------------------------------------------------------------
// FSM API
// -------------------------------------------------------------------------

// Initialize FSM and all owned sub-modules (engine, ramp).
// Must be called after spindle_init and stepper_init.
// Not hot-path; not ISR-safe.
void els_fsm_init(void);

// Advance the FSM by one tick. Called from Core 0 hot loop.
// Polls inter-core FIFO for commands, checks guards, fires transitions.
// Invokes els_engine_update and els_ramp_update internally.
// Core 0. __not_in_flash_func.
void els_fsm_step(void);

// Inject an event from Core 0 physical-button edge detect.
// The event is processed synchronously (same Core 0 context).
// Core 0. __not_in_flash_func.
void els_fsm_event(els_cmd_opcode_t cmd, uint32_t payload);

// Current FSM state. May be called from either core (read-only).
els_state_t els_fsm_get_state(void);

// Current fault code. Valid when state == ELS_STATE_FAULT.
els_fault_code_t els_fsm_get_fault(void);

#endif // ELS_FSM_H
```

### B.7.6 `els.h` — shim (public API; backward-compatible + new entries)

```c
#ifndef ELS_H
#define ELS_H

#include <stdint.h>
#include <stdbool.h>
#include "els_fsm.h"  // re-exports state enum

// -------------------------------------------------------------------------
// Backward-compatible aliases (main.c and protocol.c do not need changes)
// -------------------------------------------------------------------------
#define ELS_IDLE       ELS_STATE_IDLE
#define ELS_ENGAGED    ELS_STATE_THREADING_ENGAGED
#define ELS_FEED_HOLD  ELS_STATE_THREADING_HOLD
typedef els_state_t els_state_t;  // already defined in els_fsm.h

void  els_init(void);          // → els_fsm_init
bool  els_engage(void);        // → CMD_ARM_THREADING then waits for index (Phase 1 compat)
void  els_disengage(void);     // → CMD_DISENGAGE
void  els_feed_hold(void);     // → CMD_FEED_HOLD
void  els_resume(void);        // → CMD_RESUME
els_state_t els_get_state(void);
int32_t     els_get_error(void);  // returns z_backlog (closest analog in new engine)
bool        els_set_pitch(float pitch_mm);  // converts to ratio, calls CMD_SET_RATIO
float       els_get_pitch(void);
void        els_update(void);  // → els_fsm_step (Core 0 hot-loop entry)

// -------------------------------------------------------------------------
// New API (used by extended protocol.c and future G-code layer)
// -------------------------------------------------------------------------

// Arm threading with an explicit rational ratio (already GCD-reduced).
// Returns false and sets fault ELS_FAULT_BAD_RATIO if ratio is invalid.
bool els_arm_threading(int64_t ratio_num, int64_t ratio_den,
                       uint8_t starts, uint8_t start_index);

// Configure feed mode. feed_um_per_rev: µm per spindle revolution; axes: bitmask
// of AXIS_Z (bit 0) and/or AXIS_X (bit 1). Returns false if invalid.
bool els_start_feed(uint32_t feed_um_per_rev, uint8_t axes);

// Configure taper mode (both Z and X ratios must be set).
bool els_start_taper(int64_t z_ratio_num, int64_t z_ratio_den,
                     int64_t x_ratio_num, int64_t x_ratio_den);

// Jog one axis. direction: +1 or -1. Returns false if already in a slaved mode.
bool els_jog_start(stepper_axis_t axis, int8_t direction);
void els_jog_stop(stepper_axis_t axis);

// Index C-axis to absolute angle. angle_tenths: 0.1° units (0–3599).
bool els_index_to(uint16_t angle_tenths);

// Reset from FAULT state. Must be called explicitly by user; not automatic.
void els_reset_fault(void);

// Publish current status snapshot (call from Core 0 after els_update).
void els_status_publish(void);

// Read the latest published status (call from Core 1 protocol loop).
const els_status_t *els_status_read(void);

#endif // ELS_H
```

### B.7.7 `config.h` — new ELS fields (additions to `machine_config_t`)

```c
// — append to existing machine_config_t struct —

// Per-axis step rate ceilings (steps/sec)
uint32_t z_max_step_rate;  // default: 200000
uint32_t x_max_step_rate;  // default: 200000
uint32_t c_max_step_rate;  // default: 10000 (VFD opto limit)

// Per-axis soft limits in steps (from mm via steps_per_mm at config time)
int32_t z_soft_min_steps;  // default: INT32_MIN (disabled)
int32_t z_soft_max_steps;  // default: INT32_MAX (disabled)
int32_t x_soft_min_steps;
int32_t x_soft_max_steps;

// Backlog fault thresholds (steps)
uint32_t z_backlog_threshold;  // default: 16
uint32_t x_backlog_threshold;  // default: 16

// Ramp parameters (PIO cycles)
uint32_t z_ramp_min_delay;   // at full speed; derived from z_max_step_rate
uint32_t z_ramp_max_delay;   // at standstill; large number (~125000 = 1 step/ms)
uint32_t z_ramp_delta;       // delay change per step during accel/decel
uint32_t x_ramp_min_delay;
uint32_t x_ramp_max_delay;
uint32_t x_ramp_delta;
uint32_t c_ramp_min_delay;   // for indexing
uint32_t c_ramp_delta;

// C-axis (VFD) calibration
float    c_steps_per_rev;    // VFD steps for one spindle revolution
float    c_pulse_width_us;   // minimum VFD pulse width: 5.0 µs

// Thread table (first N entries; extend as needed)
uint16_t thread_table_count;
struct {
    float    pitch_mm;
    int64_t  ratio_num;   // precomputed GCD-reduced numerator
    int64_t  ratio_den;
    uint8_t  starts;      // default 1
} thread_table[64];
```

`config_load` must zero-fill any tail bytes when reading a blob shorter than `sizeof(machine_config_t)`, then apply field-level defaults for any zero-valued ELS fields (zero `z_max_step_rate` → 200000, etc.).

---

## B.8 State machine diagrams

### B.8.1 Top-level ELS mode FSM

```
                 ┌───────────────────────────────────────────────────────────┐
                 │  Events that transition ANY state → FAULT:                │
                 │  backlog_fault | soft_limit | rate_exceeded | estop        │
                 └───────────────────────────────────────────────────────────┘

         CMD_ARM_THREADING ──────────────────────────────►
  ┌──────┐                   ┌──────────────────┐        ┌─────────────────────┐
  │      │◄── CMD_DISARM ────│ THREADING_ARMED  │        │ THREADING_ENGAGED   │
  │      │                   └──────────────────┘        └─────────────────────┘
  │      │                    │    │                       │        │       │
  │      │                    │    │ [index_latch          │        │       │
  │      │                    │    │  && offset==0]────────►        │       │
  │      │                    │    │                   CMD_FEED_HOLD│       │
  │      │         reversal──►│    │                     (GP16)     │       │
  │      │                    ▼    │                       ▼        │       │
  │      │                  FAULT  │              ┌────────────────┐│       │
  │      │                         │              │ THREADING_HOLD ││       │
  │      │                         │              └────────────────┘│       │
  │      │                         │               CMD_RESUME (GP16)│       │
  │ IDLE │◄────────────────────────┤◄──────────────────────────────┘       │
  │      │                         │                                         │
  │      │◄── CMD_DISENGAGE ───────┘◄────────────── CMD_DISENGAGE ──────────┘
  │      │      (GP15 toggle)                          (GP15 toggle)
  │      │
  │      │──── CMD_START_FEED ────────────────────► FEED_ENGAGED
  │      │◄── CMD_DISENGAGE ─────────────────────── FEED_ENGAGED
  │      │
  │      │──── CMD_START_TAPER ───────────────────► TAPER_ENGAGED
  │      │◄── CMD_DISENGAGE ─────────────────────── TAPER_ENGAGED
  │      │
  │      │──── CMD_JOG_START ─────────────────────► JOG
  │      │◄── CMD_JOG_STOP / target_reached ──────── JOG
  │      │
  │      │──── CMD_INDEX_TO ──────────────────────► INDEXING
  │      │◄── target_reached / CMD_CANCEL ──────────  INDEXING ──► FAULT (index_lost)
  │      │
  └──────┘◄── CMD_RESET_FAULT ─────────────────── FAULT
                 (only transition out of FAULT)
```

### B.8.2 Thread-start synchronization sub-FSM (within THREADING_ARMED)

```
  ┌─────────────────┐
  │ WAITING_INDEX   │──── spindle_index_latched() ────────────────────────┐
  └─────────────────┘      (after spindle_arm_start_offset was called)     │
                                                                            ▼
                                                               ┌────────────────────┐
                                                               │ COUNTING_OFFSET    │
                                                               │ offset_remaining-- │
                                                               │ per spindle edge   │
                                                               └────────────────────┘
                                                                     │
                                                           offset_remaining == 0
                                                                     │
                                                                     ▼
                                                            → ELS_STATE_THREADING_ENGAGED
                                                              (first step emits immediately)
```

For single-start threads, `spindle_arm_start_offset(0)` is called; the index latch alone is sufficient and COUNTING_OFFSET is skipped.

For N-start pass k: `spindle_arm_start_offset(k × PPR / N)`. Validation (`PPR % N == 0`) is done in `els_arm_threading`; fault code `ELS_FAULT_MULTISTART_PPR` is set if not satisfied.

### B.8.3 Indexing controller (INDEXING state)

```
  Enter INDEXING (CMD_INDEX_TO, target_angle):
    1. Snap spindle_count at last index → c_origin
    2. Compute target_counts = (target_angle_tenths × PPR × 4) / 3600
    3. Compute delta = target_counts - (current_count - c_origin)
    4. Set C-axis direction, arm ramp
    5. Enter seek phase

  Seek phase:
    - Each Core 0 tick: els_ramp_step → emit one C-axis step if ramp not idle
    - Track c_pos_steps toward delta
    - On |position_error| ≤ 1: enter hold phase

  Hold phase:
    - Ramp idle; stepper hold (FIFO empty → PIO blocks at pull → motor torque)
    - Monitor spindle for index-pulse interval faults
    - CMD_CANCEL or E-stop exits to IDLE/FAULT

  Index-pulse sanity (running in all INDEXING ticks):
    - Expected interval: (last 4 inter-index periods averaged)
    - If new interval deviates > ±10%: set ELS_FAULT_INDEX_LOST → FAULT
```

---

## B.9 Hot-loop execution order (Core 0)

```
// One iteration, ~20 µs budget
spindle_update()               // drain DMA ring; update count, rate, index latch
els_fsm_step()                 // poll inter-core FIFO; check guards; fire transitions;
                               //   internally calls els_engine and els_ramp
safety_watchdog_feed()
safety_debounce_update()
button_edge_detect()           // GP15/16/17 → els_fsm_event()
els_status_publish()           // double-buffer swap
// no sleep_us — free-running to maximize FIFO drain headroom
```

The `sleep_us(20)` in [`firmware/src/main.c:117`](../firmware/src/main.c#L117) is removed in the rewrite. Core 0 free-runs; loop period is determined by instruction count. At ~200 instructions per iteration @ 125 MHz the loop is sub-µs, providing ample DMA-ring drain rate.

---

## B.10 `protocol.h/.c` extensions

New JSON fields in status tx (`status_snapshot_t` equivalent):

```json
{
  "els_state": 2,
  "els_fault": 0,
  "z_backlog": 0,
  "x_backlog": 0,
  "index_latched": false,
  "c_pos": 0
}
```

New JSON commands in rx parser:

| Command key | Payload fields | Maps to |
|---|---|---|
| `"arm"` | `pitch`, `starts`, `start_index` | `els_arm_threading` |
| `"feed"` | `rate_um_rev`, `axes` | `els_start_feed` |
| `"taper"` | `z_num`, `z_den`, `x_num`, `x_den` | `els_start_taper` |
| `"jog"` | `axis`, `dir` | `els_jog_start` |
| `"jog_stop"` | `axis` | `els_jog_stop` |
| `"index"` | `angle_tenths` | `els_index_to` |
| `"disengage"` | — | `els_disengage` |
| `"feed_hold"` | — | `els_feed_hold` |
| `"resume"` | — | `els_resume` |
| `"reset_fault"` | — | `els_reset_fault` |

---

## B.11 Unit-test surface (host-runnable targets)

Each of the following must have a host-compiled test under [`firmware/test/`](../firmware/test/) with mocked dependencies following the existing pattern:

| Test file | Module under test | Key invariants checked |
|---|---|---|
| `test_bresenham.c` | `els_engine_axis_advance` | Zero cumulative error over 1M edges at 50+ distinct ratios |
| `test_predictor.c` | `els_engine_axis_advance` (rate field) | Predicted delay within ±20% of actual at steady rate |
| `test_ramp.c` | `els_ramp_step`, `els_ramp_floor` | Trapezoidal profile shape; no negative delays; idle after disengage |
| `test_fsm.c` | `els_fsm_step`, `els_fsm_event` | All transitions reachable; FAULT sticky; bad transitions silently ignored |
| `test_ratio.c` | GCD reduction + rate validation in `els_arm_threading` | Correct reduction; invalid ratios rejected; PPR-divisibility enforced |
| `test_multistart.c` | `spindle_arm_start_offset` + countdown | Correct offset for k=0..N-1 at N=1,2,3,4,6,8 |
| `test_softlimit.c` | `els_engine_axis_flush` (limit path) | Fault returned on first step crossing limit, no step emitted |

---

## B.12 Build integration

Add to `firmware/CMakeLists.txt`:

```cmake
target_sources(superdro PRIVATE
    src/spindle.c
    src/els_engine.c
    src/els_ramp.c
    src/els_fsm.c
    # els.c replaces the existing target_sources entry
)
```

The `quadrature.pio` PIO header target (`superdro_quadrature_pio_h`) already exists. No new `pioasm` targets are needed — the 3-pin variant is compiled from the same `.pio` file with a new program block (or an `.define` switch that selects 2-pin vs 3-pin program at C init time).

The `stepper.pio` target (`superdro_stepper_pio_h`) is unchanged; the C init code instantiates it three times with different `Y` preload values for Z, X, and C.

---

## B.13 Open questions resolved

None. Layer C may proceed.
