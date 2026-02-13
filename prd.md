# Lathe Controller — Project Plan v3 (Final)

## Concept

A Raspberry Pi Pico W-based lathe controller for a 12"+ workshop lathe with a 10" Android tablet as the UI/display. Combines DRO, electronic leadscrew (ELS), and conversational turning into a single system.

**Architecture**: Pico W = hard real-time motion control & sensor IO. Android = display, job setup, parameter entry only. Communication via USB serial.

---

## All Decisions

| Decision | Choice |
|---|---|
| Axes | Z + X + Spindle. X is DRO now, stepper-ready for future |
| Spindle encoder | Optical, 1000+ PPR |
| Z drive | Closed-loop stepper (CL57T), belt-driven to existing 6mm pitch leadscrew |
| Z DRO | Glass scale or magnetic encoder (quadrature) — independent position feedback |
| X drive | DRO only — supports both glass scale (TTL) and magnetic encoder |
| Display | 10" Android tablet |
| Physical controls | Minimal (E-stop, engage, feed hold, cycle start). Expandable later |
| Comms | USB Serial (CDC ACM) |
| Threading retract | User-selectable: rapid retract or spring pass |
| Configuration | All machine params configurable (leadscrew pitch, encoder PPR, steps/rev, etc.) |

---

## Firmware Math

### Default Configuration (all configurable)

```c
// config.h — Machine parameters (runtime-overridable via serial or flash storage)
typedef struct {
    // Spindle
    uint16_t spindle_ppr;           // 1000
    uint8_t  spindle_quadrature;    // 4 (4x decode)
    uint32_t spindle_counts_per_rev; // 4000 (ppr × quadrature)
    uint16_t spindle_max_rpm;       // 3500

    // Z axis — scale (DRO) + stepper (ELS)
    float    z_scale_resolution_mm; // 0.005 (5µm for typical glass scale)
    float    z_leadscrew_pitch_mm;  // 6.0
    uint16_t z_steps_per_rev;       // 1000 (CL57T configurable: 200-10000)
    float    z_belt_ratio;          // 1.0 (motor:leadscrew, e.g. 2.0 = 2:1 reduction)
    float    z_steps_per_mm;        // z_steps_per_rev * z_belt_ratio / z_leadscrew_pitch_mm
    float    z_max_speed_mm_s;      // 50.0
    float    z_accel_mm_s2;         // 100.0
    float    z_backlash_mm;         // 0.0 (measure and set)
    float    z_travel_min_mm;       // -500.0 (soft limit)
    float    z_travel_max_mm;       // 0.0 (soft limit)

    // X axis
    float    x_scale_resolution_mm; // 0.005 (5µm for typical glass scale)
    bool     x_is_diameter;         // true (display as diameter, internal = radius)
    float    x_travel_min_mm;       // -200.0
    float    x_travel_max_mm;       // 0.0

    // Future X stepper
    uint16_t x_steps_per_rev;       // 1000
    float    x_leadscrew_pitch_mm;  // 3.0
    float    x_belt_ratio;          // 1.0
    float    x_steps_per_mm;        // derived

    // Threading
    uint8_t  thread_retract_mode;   // 0 = rapid retract X, 1 = spring pass, user-selectable
    float    thread_retract_x_mm;   // 1.0 (clearance for rapid retract)
    float    thread_compound_angle;  // 29.5° (infeed angle for multi-pass)
} machine_config_t;
```

### Key Calculations

**Z resolution** (default config):
- CL57T at 1000 steps/rev, 1:1 belt, 6mm leadscrew
- `z_steps_per_mm = 1000 × 1.0 / 6.0 = 166.67 steps/mm`
- Resolution: `1/166.67 = 0.006mm per step` (6µm)
- For finer resolution: set CL57T to 4000 steps/rev → `666.67 steps/mm` → 1.5µm/step

**Max step frequency at max RPM threading**:
- Worst case: coarsest thread (6mm pitch) at 3500 RPM
- Z speed = `6.0mm × 3500/60 = 350 mm/s`
- Step freq = `350 × 166.67 = 58,333 Hz` (trivial for PIO)
- At 4000 steps/rev: `350 × 666.67 = 233,333 Hz` (still easy for PIO)

**Spindle encoder frequency**:
- 1000 PPR × 4 (quadrature) × 3500 RPM / 60 = `233,333 Hz`
- PIO handles this with zero issue (capable of >10 MHz)

---

## Hardware Architecture

### Block Diagram

```
┌─────────────────────────────────────────────────┐
│                 ANDROID TABLET (10")             │
│  ┌───────────────────────────────────────────┐   │
│  │  DRO Display  │  Thread Sel  │  Cycle UI  │   │
│  └───────────────────────────────────────────┘   │
│                      USB Serial                  │
└────────────────────────┬────────────────────────┘
                         │
┌────────────────────────┴────────────────────────┐
│               RASPBERRY PI PICO W                │
│                                                  │
│  Core 0: Real-time control loop                  │
│    - Spindle → Z step rate (ELS)                 │
│    - Motion planner (accel/decel)                │
│    - Soft limits                                 │
│                                                  │
│  Core 1: Comms + housekeeping                    │
│    - USB serial TX/RX                            │
│    - DRO reporting                               │
│    - Command parsing                             │
│                                                  │
│  PIO 0: Spindle + X encoder decode               │
│  PIO 1: Z stepper (+ future X stepper)           │
└──┬──────┬──────┬──────┬──────┬──────┬───────────┘
   │      │      │      │      │      │
   ▼      ▼      ▼      ▼      ▼      ▼
Spindle  X-axis  Z-axis  E-stop  Engage  Feed
Encoder  Scale   CL57T   (HW)   Button   Hold
1000PPR  (TTL/   Driver
         mag)
```

### Pico GPIO Pinout

```
GP0  - UART0 TX  → Android USB serial
GP1  - UART0 RX  ← Android USB serial
GP2  - Spindle Encoder A       (PIO 0, SM 0)
GP3  - Spindle Encoder B       (PIO 0, SM 0)
GP4  - Spindle Index (Z pulse) (GPIO interrupt)
GP5  - X Scale A / CLK         (PIO 0, SM 1)
GP6  - X Scale B / DATA        (PIO 0, SM 1)
GP7  - (reserved: X Index)

GP8  - Z Step                  (PIO 1, SM 0)
GP9  - Z Direction             (GPIO output)
GP10 - Z Enable                (GPIO output)
GP11 - (reserved: X Step)      (PIO 1, SM 1, future)
GP12 - (reserved: X Direction) (future)
GP13 - (reserved: X Enable)    (future)

GP14 - E-stop input            (interrupt, active low, HW pull-up)
GP15 - Engage/disengage button (interrupt, active low)
GP16 - Feed hold button        (interrupt, active low)
GP17 - Cycle start button      (interrupt, active low)
GP18 - (reserved: jog fwd)
GP19 - (reserved: jog rev)
GP20 - Z Scale A / CLK         (PIO 0, SM 2)
GP21 - Z Scale B / DATA        (PIO 0, SM 2)
GP22 - (reserved: feed override)

GP25 - (Pico W: wireless SPI CS — not available for user GPIO)
GP26 - (ADC0, reserved)
GP27 - (ADC1, reserved: feed override analog)
GP28 - (ADC2, reserved)

LED  - Onboard LED via CYW43 WL_GPIO0 (cyw43_arch_gpio_put)
```

**Pico W internal GPIO (not on headers, not available for user use)**:
- GP23 → CYW43 wireless power-on (WL_ON)
- GP24 → CYW43 wireless SPI data/IRQ
- GP25 → CYW43 wireless SPI chip-select
- GP29 → CYW43 wireless SPI clock (VSYS ADC3 time-shared with wireless)
```

### Scale Abstraction (X and Z axes)

Both X and Z axes use glass scales or magnetic encoders for DRO position feedback. The Z axis additionally has a stepper for ELS/turning — the scale provides independent position verification.

| Axis | GPIO Pins | PIO | Config Resolution Key |
|---|---|---|---|
| X scale | GP5/GP6 | PIO 0, SM 1 | `x_scale_resolution_mm` |
| Z scale | GP20/GP21 | PIO 0, SM 2 | `z_scale_resolution_mm` |

Both scale types output quadrature signals, so the same `quadrature.pio` program handles either:

| Scale Type | Interface | PIO Program | Resolution |
|---|---|---|---|
| Glass scale (TTL) | Quadrature A/B | `quadrature.pio` (same as spindle) | Typically 5µm |
| Magnetic encoder | Quadrature A/B | `quadrature.pio` (same as spindle) | Varies by strip pitch |

The resolution config value maps counts to mm. If a scale uses SPI/SSI instead, a separate PIO program can be loaded — the abstraction layer handles this:

```c
// encoder.h
typedef struct {
    int32_t raw_count;
    float   position_mm;
} axis_position_t;

// Returns position regardless of underlying scale type
axis_position_t x_axis_read(void);
axis_position_t z_axis_read(void);
```

**Z position sources**: In DRO mode (Phase 1), Z position comes from the scale. In ELS/threading mode (Phase 2+), the stepper drives Z but the scale provides closed-loop verification.

### PIO Allocation

| PIO Block | State Machine | Function |
|---|---|---|
| PIO 0, SM 0 | Spindle quadrature decode | 4x → 4000 counts/rev |
| PIO 0, SM 1 | X-axis scale decode | Quadrature (TTL glass or magnetic) |
| PIO 0, SM 2 | Z-axis scale decode | Quadrature (TTL glass or magnetic) |
| PIO 1, SM 0 | Z step pulse generation | Frequency-controlled |
| PIO 1, SM 1 | (reserved: X step pulses) | Future |

---

## Bill of Materials

| Item | Example Part | Est. Cost |
|---|---|---|
| Raspberry Pi Pico W | RP2040 + CYW43439 WiFi/BT | $6 |
| Spindle encoder | Omron E6B2-CWZ6C 1000PPR | $25-50 |
| X-axis glass scale | 200-300mm, 5µm, TTL output | $30-60 |
| Z-axis glass scale | 500-600mm, 5µm, TTL output | $40-80 |
| Z stepper motor | NEMA 23, 2-3 Nm | $25-40 |
| Z stepper driver | CL57T closed-loop | $40-60 |
| Timing belt + pulleys | GT2/HTD 3M, 1:1 ratio for 6mm leadscrew | $15-25 |
| 10" Android tablet | USB OTG required | $100-150 |
| E-stop button | Panel mount, NC | $5 |
| Momentary buttons ×3 | Engage, feed hold, cycle start | $5-10 |
| Power supply | 48V stepper + 5V Pico | $20-30 |
| Enclosure, connectors, wiring | | $20-30 |
| **Total** | | **~$290-460** |

---

## Phased Development

### Phase 1 — DRO

**Goal**: Position display on Android for X, Z, spindle RPM.

**Pico firmware**:
- PIO quadrature decode: spindle (SM 0), X scale (SM 1), Z scale (SM 2)
- Z position from dedicated scale (independent of stepper)
- JSON status at ~50 Hz: `{"pos":{"x":...,"z":...},"rpm":...,"state":"idle"}`
- RPM from spindle encoder delta over 50ms window
- Config struct loaded from flash, updatable via serial

**Android app**:
- Large high-contrast DRO digits (X, Z, RPM)
- Inch/metric toggle
- Zero / preset per axis
- Diameter/radius mode for X
- Tool offset table (stored in app, sent to Pico)
- Connection status indicator

**Milestone**: Live X + RPM display while manually turning.

### Phase 2 — Electronic Leadscrew

**Goal**: Synchronize Z stepper to spindle for thread cutting.

**Pico firmware**:
- Core 0 tight loop (~20µs period):

```c
spindle_pos = pio_read_spindle_count();
z_target_steps = (int64_t)spindle_pos * pitch_steps / counts_per_rev;
z_error = z_target_steps - z_actual_steps;
step_freq = proportional_control(z_error, config.z_accel_mm_s2);
pio_set_step_freq(step_freq);
```

- Absolute position tracking with spindle index for multi-pass alignment
- Thread table: metric (0.5–6mm), imperial (8–56 TPI), modular, diametral pitch
- RH/LH direction
- Engage/disengage with acceleration ramp
- Stall detection (RPM drop → hold Z, alarm)
- CL57T alarm input monitoring

**Android app**:
- Thread type tabs: Metric / Imperial / Modular / DP
- Pitch picker (common values) + custom entry
- Live: pitch, direction, RPM, Z pos, pass count
- Threading state: disengaged → engaged → holding
- Retract mode selector: rapid retract / spring pass

**Safety**: Physical engage button only. E-stop hardware path. Feed hold decels gracefully.

**Milestone**: Cut M12×1.75, verify multi-pass alignment over 5+ passes.

### Phase 3 — Conversational Turning

**Goal**: Canned cycles on Android, executed by Pico. **Requires X stepper.**

| Operation | Description |
|---|---|
| Facing | X feed to center, configurable feedrate (mm/rev or mm/min) |
| Straight turning | Z feed to length |
| Taper turning | Coordinated X+Z, angle or ratio input |
| Grooving | X plunge, optional Z step-over |
| Threading (enhanced) | Multi-pass, compound infeed angle, spring pass option |
| Chamfer | Entry/exit on turning ops |

**Pico firmware**:
- DDA linear interpolation for X+Z coordination
- Feedrate modes: mm/rev (spindle-synced), mm/min (time-based)
- State machine: `IDLE → RAPID_TO_START → CUTTING → RETRACT → COMPLETE`
- Thread multi-pass scheduler: decreasing depth per pass, configurable compound angle (default 29.5°)
- Retract strategy per operation: rapid retract X or spring pass (user-selected)
- Soft limits, feed hold (controlled decel + resume), abort (retract X, stop Z)

**Android app**:
- Operation cards with parameter entry + sensible defaults
- 2D cross-section toolpath preview (Canvas)
- Pass calculator (material, DOC → suggested passes)
- Cycle start / feed hold / abort (mirror physical)
- Operation log, repeat-last

**Milestone**: Face workpiece, cut taper, groove — all via conversational UI.

### Phase 4 — Refinements (Future)

#### Bluetooth/BLE Communication Option

**Goal**: Enable wireless tablet operation for convenience and setup. Safety-critical operations (threading, active cuts) remain USB-only.

**Hardware**:
- Pico W onboard CYW43439 WiFi/BLE radio
- Leverage built-in BLE radio via CYW43 driver (Pico W selected as target board)

**Pico firmware**:
- Dual-transport layer: USB (primary) and BLE (secondary) with fallback logic
- Message protocol identical to USB (JSON, same commands/status)
- BLE transport: GATT profile with:
  - Characteristic 1: `status_write` (Android → Pico commands)
  - Characteristic 2: `status_notify` (Pico → Android streaming at 10 Hz over BLE, lower latency requirement than USB)
- Failover logic:
  - Preferred mode: USB (wired, guaranteed)
  - If USB unplugged: switch to BLE if connected, otherwise buffer commands and alert
  - Threading/active cycles: require USB; reject BLE-initiated cycle start with error message
  - Heartbeat: BLE heartbeat every 500ms; loss of 3 heartbeats → feed hold, request USB reconnection
- Queue mechanism for BLE loss recovery: buffer up to 100 status messages, resync on reconnect

**Android app**:
- Device scanner screen: detect nearby BLE devices
- Connect / disconnect / force USB buttons
- UI indication of active transport (USB icon vs. BLE icon)
- For BLE-only mode: disable cycle start, thread start buttons; show "USB required for cutting" warning
- Settings: allow user to set BLE preferred fallback (convenience mode) vs. strict USB-only

**Milestone**: Wireless X/Z/RPM display and parameter setup. All active turning operations via USB.

**Scope**: Phase 4 only. Deferred until Phase 1-3 stable and robust.

---

#### Other Phase 4 Refinements

- Tool nose radius compensation
- Multi-op sequencing (rough → finish → chamfer)
- MPG handwheel (GP20-21)
- Feed override pot (GP27 ADC, 50-150%)
- Part counter, cycle time logging
- Export/import params (JSON on Android)
- Wear offset adjustments
- Bore operations (internal threading, boring bar cycles)

---

## Communication Protocol

### Transport Modes

| Mode | Medium | Baud/Latency | Safety | Use Case |
|---|---|---|---|---|
| **USB Serial (CDC ACM)** | USB Type-A (wired) | 115200 baud, ~10ms | Primary (hard E-stop independent) | Primary operation, guaranteed reliability |
| **Bluetooth/BLE (Phase 4)** | Wireless 2.4GHz | ~50-100ms latency | Secondary (feed hold via heartbeat) | Wireless tablet convenience, non-safety-critical monitoring |

**Note**: Threading and active cuts **require USB** (primary comms). BLE is for positioning display and parameter setup only. E-stop is always hardware and independent of comms mode.

### Status (Pico → Android, ~50 Hz)

```json
{"pos":{"x":12.450,"z":-35.200},"rpm":820,"state":"threading","pitch":1.5,"dir":"rh","pass":3,"retract":"rapid","fh":false,"comms_mode":"usb"}
```

States: `idle`, `jogging`, `threading`, `cycle`, `feed_hold`, `alarm`

**Phase 4 addition**: `comms_mode` field reports active transport (`usb` or `ble`)

### Commands (Android → Pico)

```json
{"cmd":"zero","axis":"x"}
{"cmd":"preset","axis":"z","value":-50.0}
{"cmd":"set_pitch","pitch":1.5,"dir":"rh"}
{"cmd":"set_retract","mode":"spring"}
{"cmd":"engage"}
{"cmd":"disengage"}
{"cmd":"cycle_start","op":"face","params":{"start_x":25.0,"end_x":0,"feed":0.15,"mode":"mm_rev"}}
{"cmd":"cycle_start","op":"thread","params":{"pitch":1.5,"dir":"rh","start_z":0,"end_z":-20,"depth":0.92,"passes":6,"retract":"spring"}}
{"cmd":"feed_hold"}
{"cmd":"abort"}
{"cmd":"set_offset","tool":1,"x":0.0,"z":(USB or BLE) 0.0}
{"cmd":"jog","axis":"z","dir":1,"speed":2.0}
{"cmd":"config_set","key":"z_leadscrew_pitch_mm","value":6.0}
{"cmd":"config_get","key":"z_leadscrew_pitch_mm"}
{"cmd":"config_save"}
```

Newline-delimited JSON @ 115200 baud. Every command acknowledged: `{"ack":"cmd","ok":true}` or `{"ack":"cmd","ok":false,"err":"reason"}`.

### Config Protocol

Machine params are read/write via serial and persisted to Pico flash:

```json
{"cmd":"config_get","key":"z_leadscrew_pitch_mm"}
→ {"ack":"config_get","ok":true,"key":"z_leadscrew_pitch_mm","value":6.0}

{"cmd":"config_set","key":"z_steps_per_rev","value":4000}
→ {"ack":"config_set","ok":true}

{"cmd":"config_save"}
→ {"ack":"config_save","ok":true}

{"cmd":"config_list"}
→ {"ack":"config_list","ok":true,"params":{...all key/value pairs...}}
```

Android app has a "Machine Setup" screen that reads/writes these values.

---

## Safety Architecture

| Layer | Mechanism |
|---|---|
| **Hardware** | E-stop NC → CL57T enable line AND Pico W GP14. Motor power killed independent of firmware |
| **Firmware** | Soft limits on all moves. Feed hold = controlled decel. Abort = retract X, stop Z |
| **Watchdog** | Pico W watchdog resets if Core 0 stalls → all steppers disabled |
| **Comms loss** | USB loss = immediate feed hold (primary). BLE loss (Phase 4) = fallback to hardcoded defaults, finish current pass, then hold. Reconnect via USB recommended. |
| **Spindle stall** | RPM < threshold during threading → hold Z, alarm |
| **CL57T alarm** | Driver position error alarm → Pico reads alarm pin, disables, alerts Android |
| **Retract** | Abort always retracts X first (away from work), then stops Z |

**Rule**: Pico is safe standalone. Android is convenience, not safety. Physical controls always override.

---

## Tech Stack

| Layer | Technology |
|---|---|
| Pico W firmware | C/C++ with Pico SDK + CYW43 driver, PIO assembly |
| Android app | Kotlin, Jetpack Compose |
| Comms (Phase 1-3) | USB Serial (CDC ACM), 115200 baud (primary) |
| Comms (Phase 4) | Bluetooth/BLE via external module (optional wireless fallback) |
| 2D preview | Android Canvas |
| Config storage | Pico W flash (wear-leveled sector) |
| Build | CMake (Pico), Gradle (Android) |
| VCS | Git monorepo |

---

## Mechanical Notes

### Belt Drive (Z-axis)

With 6mm leadscrew pitch and CL57T:

| CL57T Steps/Rev | Belt Ratio | Steps/mm | Resolution | Max Step Freq @ 3500RPM, 6mm pitch |
|---|---|---|---|---|
| 1000 | 1:1 | 166.7 | 6.0 µm | 58 kHz |
| 1000 | 2:1 | 333.3 | 3.0 µm | 117 kHz |
| 4000 | 1:1 | 666.7 | 1.5 µm | 233 kHz |
| 4000 | 2:1 | 1333.3 | 0.75 µm | 467 kHz |

**Recommendation**: Start with CL57T at 1000 steps/rev, 1:1 belt. 6µm resolution is fine for most turning. Increase driver microstepping if needed — no hardware changes required.

- **Belt**: GT2 6mm wide for light-medium loads, HTD 3M for heavier cuts
- **Tensioner**: Spring-loaded idler recommended
- **Disengage**: Hinged motor mount or quick-release idler for manual operation

### Spindle Encoder Mounting

- Mount rear of spindle or belt-drive 1:1 from spindle
- Shield from chips/coolant (enclosure or labyrinth seal)
- Shielded cable, route away from stepper cables
- Max encoder frequency: 1000 PPR × 4 × 3500/60 = 233 kHz (PIO handles easily)

---

## Project Structure

```
lathe-controller/
├── firmware/
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.c              # Init, core assignment, main loops
│   │   ├── config.c/.h         # Machine config struct, flash storage
│   │   ├── encoder.c/.h        # Spindle + X scale abstraction
│   │   ├── stepper.c/.h        # Z (+ future X) step generation
│   │   ├── els.c/.h            # Electronic leadscrew (spindle sync)
│   │   ├── motion.c/.h         # Motion planner, DDA interpolation
│   │   ├── cycles.c/.h         # Canned cycle definitions (Phase 3)
│   │   ├── protocol.c/.h       # JSON serial comms + command dispatch
│   │   ├── safety.c/.h         # E-stop, limits, watchdog, alarms
│   │   └── threads.h           # Thread pitch lookup tables
│   └── pio/
│       ├── quadrature.pio      # Encoder/scale decode (shared)
│       └── stepper.pio         # Step pulse generation
├── android/
│   ├── app/src/main/java/.../
│   │   ├── ui/
│   │   │   ├── DroScreen.kt
│   │   │   ├── ThreadingScreen.kt
│   │   │   ├── CycleScreen.kt
│   │   │   ├── ConfigScreen.kt
│   │   │   └── PreviewCanvas.kt
│   │   ├── serial/
│   │   │   ├── UsbSerialManager.kt
│   │   │   └── Protocol.kt
│   │   ├── model/
│   │   │   ├── MachineState.kt
│   │   │   ├── ThreadTable.kt
│   │   │   └── CycleParams.kt
│   │   └── viewmodel/
│   │       ├── DroViewModel.kt
│   │       └── CycleViewModel.kt
│   └── build.gradle
├── docs/
│   ├── wiring-diagram.md
│   ├── bom.md
│   ├── protocol.md
│   └── setup-guide.md
└── README.md
```

---

## Phase 1 Implementation Plan — DRO

### Dependency Graph

```
#1 Project Setup (monorepo, toolchain, git)
 ├──► #2 PIO Quadrature Decoder ──┐
 ├──► #4 Config System (flash) ───┼──► #3 Encoder Abstraction ──┐
 │    └──► #5 USB Serial Protocol ────────────────────────────────┤
 ├──► #6 Safety Module ──────────────────────────────────────────┤
 │                                                                ▼
 │                                                    #7 Firmware main.c ──┐
 │                                                                          │
 └──► #8 Android Project Setup                                              │
      └──► #9 MachineState + ViewModel                                      │
           ├──► #10 DRO Display Screen ─────────────────────────────────────┤
           └──► #11 Config Screen ──────────────────────────────────────────┤
                                                                            ▼
                                                            #12 Integration Testing
```

### Firmware Track

#### Task 1 — Initialize monorepo project structure
- Set up monorepo in `/superdro` with `firmware/`, `android/`, `docs/`
- `firmware/CMakeLists.txt` with Pico SDK integration
- `firmware/src/` and `firmware/pio/` directories
- Android Gradle project scaffold with Kotlin/Compose
- `.gitignore`, `README.md`
- Initialize git repo
- Verify Pico SDK toolchain builds (cmake, arm-none-eabi-gcc)
- **Blocked by**: nothing

#### Task 2 — PIO quadrature decoder program
- Write `pio/quadrature.pio` for 4x quadrature decoding
- Shared by spindle encoder (PIO 0 SM 0, GP2/GP3), X-axis scale (PIO 0 SM 1, GP5/GP6), and Z-axis scale (PIO 0 SM 2, GP20/GP21)
- Decode A/B quadrature signals → 32-bit signed counter
- Readable from C via PIO FIFO or direct SM register read
- Must handle signal rates up to 233 kHz (spindle at 3500 RPM)
- **Blocked by**: #1

#### Task 3 — Encoder abstraction layer (`encoder.c/.h`)
- Initialize PIO 0 SM 0 for spindle (GP2/GP3), SM 1 for X scale (GP5/GP6), SM 2 for Z scale (GP20/GP21)
- Spindle index pulse handling on GP4 (GPIO interrupt) for revolution counting
- `axis_position_t` struct: `raw_count`, `position_mm`
- `spindle_read()` → raw count
- `x_axis_read()` → position in mm (using `x_scale_resolution_mm` from config)
- `z_axis_read()` → position in mm (using `z_scale_resolution_mm` from config)
- `z_axis_zero()` / `z_axis_preset(value_mm)` — same pattern as X
- RPM calculation from spindle count delta over configurable window (~50ms)
- Spindle direction detection
- **Blocked by**: #2, #4

#### Task 4 — Machine config system (`config.c/.h`)
- `machine_config_t` struct with all fields from PRD
- Default values as specified (spindle_ppr=1000, z_leadscrew_pitch_mm=6.0, etc.)
- Flash storage: save/load config to Pico W flash (wear-leveled sector)
- `config_get(key)` / `config_set(key, value)` / `config_save()` / `config_load()`
- `config_list()` to enumerate all params
- Derived value calculation (e.g., `z_steps_per_mm` from steps_per_rev, belt_ratio, leadscrew_pitch)
- **Blocked by**: #1

#### Task 5 — USB serial protocol (`protocol.c/.h`)
- USB CDC ACM setup at 115200 baud
- TX: JSON status at ~50 Hz: `{"pos":{"x":...,"z":...},"rpm":...,"state":"idle"}`
- RX: Parse newline-delimited JSON commands
- Command dispatch for Phase 1:
  - `zero` (axis) — zero an axis
  - `preset` (axis, value) — set axis to value
  - `config_get` / `config_set` / `config_save` / `config_list`
  - `set_offset` (tool, x, z) — tool offsets
- ACK/NACK responses: `{"ack":"cmd","ok":true}` or `{"ack":"cmd","ok":false,"err":"reason"}`
- Lightweight JSON parser (cJSON or minimal hand-rolled)
- Runs on Core 1
- **Blocked by**: #4

#### Task 6 — Safety module (`safety.c/.h`)
- E-stop input on GP14 (interrupt, active low, HW pull-up) → sets alarm state
- Watchdog timer → resets Pico W if Core 0 stalls
- Comms loss detection (no heartbeat 2s → alarm, wired now for Phase 2)
- Alarm state management and reporting to Android
- LED status indicator via CYW43 WL_GPIO0 (heartbeat blink pattern, requires cyw43_arch driver)
- **Blocked by**: #1

#### Task 7 — Firmware main.c (dual-core wiring)
- Core 0 real-time loop (~20µs period):
  - Read spindle encoder, calculate RPM
  - Read X-axis scale position
  - Read Z-axis scale position (via `z_axis_read()`)
  - Check safety (E-stop, soft limits)
- Core 1 communications:
  - USB serial TX (status JSON at 50 Hz)
  - USB serial RX (command parsing and dispatch)
- Shared state between cores (lock-free or spin locks)
- Init sequence: `config_load → PIO init → encoder init → safety init → start cores`
- Button debouncing for physical controls (GP15-17)
- **Blocked by**: #3, #5, #6

### Android Track

#### Task 8 — Android project setup with USB serial
- Kotlin + Jetpack Compose, min SDK API 26+
- Add `usb-serial-for-android` dependency (CDC ACM)
- `UsbSerialManager.kt`: device detection, connect/disconnect, USB permission requests
- `Protocol.kt`: JSON parsing for status messages, command serialization
- Single activity, Compose navigation scaffold
- USB permission manifest entries
- Landscape orientation lock for 10" tablet
- **Blocked by**: #1

#### Task 9 — MachineState model and DroViewModel
- `MachineState.kt`: data class with x_pos, z_pos (mm), rpm, state, connection status
- App-side state: inch/metric mode, diameter/radius mode
- `DroViewModel.kt`:
  - Receives serial status updates → updates MachineState StateFlow
  - Zero/preset commands (sends to Pico via protocol)
  - Unit conversion (mm ↔ inch)
  - Diameter ↔ radius conversion for X
  - Tool offset table (stored locally in app, sent to Pico)
  - Connection state management (connected / disconnected / error)
- **Blocked by**: #8

#### Task 10 — DRO display screen (`DroScreen.kt`)
- Large high-contrast digits for X, Z, spindle RPM
- Dark background, bright digits (workshop-visible, readable from arm's length)
- Touch-to-zero per axis (long press or dedicated button)
- Preset entry dialog per axis
- Inch/metric toggle button (converts all displayed values)
- Diameter/radius toggle for X axis
- Connection status indicator (green/red/yellow)
- RPM display with smoothed update rate (no flicker)
- Landscape layout optimized for 10" tablet
- **Blocked by**: #9

#### Task 11 — Config screen (`ConfigScreen.kt`)
- Reads all config from Pico via `config_list` command
- Params grouped by category: Spindle, Z-axis, X-axis, Threading
- Edit fields with appropriate input types (numeric, boolean)
- Save → sends `config_set` for each changed param, then `config_save`
- Validation on Android side (reasonable ranges)
- Show derived values (`z_steps_per_mm`) as read-only
- Connection-required indicator
- **Blocked by**: #9

### Integration

#### Task 12 — End-to-end integration testing
- Flash firmware to Pico W, connect to Android tablet via USB
- Verify connection establishment and status streaming
- Spin spindle encoder by hand → verify RPM on tablet
- Move X scale → verify X position updates on tablet
- Test zero/preset commands from tablet
- Test inch/metric and diameter/radius toggles
- Test config read/write/save cycle
- Verify E-stop input triggers alarm state on tablet
- Check 50 Hz update rate is smooth (no jitter/lag)
- Test reconnection after USB disconnect/reconnect
- **Milestone**: Live X + RPM display while manually turning
- **Blocked by**: #7, #10, #11

### Parallel Workstreams

The firmware track (#2-7) and Android track (#8-11) can be developed in parallel after task #1. The critical path is:

```
Firmware:  #1 → #2 → #3 → #7 ──┐
                                 ├──► #12 (integration)
Android:   #1 → #8 → #9 → #10 ─┘
```
