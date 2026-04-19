# SuperDRO — Lathe Controller

## Project Overview

Raspberry Pi Pico W-based lathe controller combining DRO, electronic leadscrew (ELS), and conversational turning. 10" Android tablet as UI. See `prd.md` for full spec.

## Architecture

- **Pico W (RP2040 + CYW43439)**: Hard real-time motion control & sensor IO (C/C++, Pico SDK + CYW43 driver, PIO assembly)
- **Android tablet**: Display, job setup, parameter entry only (Kotlin, Jetpack Compose)
- **Comms**: USB Serial (CDC ACM), 115200 baud, newline-delimited JSON

## Project Structure

```
superdro/
├── firmware/          # Pico W C/C++ firmware (CMake + Pico SDK)
│   ├── src/           # C source files
│   └── pio/           # PIO assembly programs
├── android/           # Kotlin/Compose Android app (Gradle)
├── webapp/            # Go + HTML/JS development companion (USB serial → browser)
│   ├── cmd/superdro-web/  # Entry point
│   ├── internal/      # Serial manager + WebSocket hub
│   └── static/        # HTML/CSS/JS frontend
├── docs/              # Wiring diagrams, BOM, protocol docs
└── prd.md             # Full PRD with Phase 1 implementation plan
```

## Current Phase

**Phase 2 — ELS**: Electronic leadscrew (Z-axis threading/feed, spindle-synchronised). Spec in `docs/superdroElsPrd.md`, architecture in `docs/els-layer-b.md`, bring-up procedure in `docs/els-bring-up.md`.

### Implementation Status (Phase 1 — DRO)

- [x] Task 1: Monorepo project structure
- [x] Task 2: PIO quadrature decoder (`pio/quadrature.pio`)
- [x] Task 3: Encoder abstraction (`src/encoder.c/.h`)
- [x] Task 4: Config system (`src/config.c/.h`)
- [x] Task 5: USB serial protocol (`src/protocol.c/.h`)
- [x] Task 6: Safety module (`src/safety.c/.h`)
- [x] Task 7: Firmware main.c (dual-core wiring)
- [x] Task 8: Android project setup with USB serial
- [x] Task 9: MachineState model + DroViewModel
- [x] Task 10: DRO display screen
- [x] Task 11: Config screen
- [ ] Task 12: Integration testing (requires hardware)

### Implementation Status (Phase 2 — ELS)

- [x] `src/spindle.c/.h` — spindle encoder ring buffer, index latch, multi-start offset, rate estimation
- [x] `src/els_engine.c/.h` — Bresenham ratio engine, soft limits, backlog tracking
- [x] `src/els_ramp.c/.h` — trapezoidal accel/decel ramp, `els_ramp_floor()` for ramp-limited step delay
- [x] `src/els_fsm.c/.h` — FSM: IDLE / THREADING_ARMED / ENGAGED / HOLD / FEED / JOG / INDEXING / FAULT
- [x] `src/els.c/.h` — shim API: `els_set_pitch`, `els_engage`, `els_disengage`, `els_feed_hold`, `els_resume`
- [x] `src/config.c` — flash buffer enlarged to sector size; `config_get_mutable()` for dynamic thread table
- [x] `src/stepper.c/.h` — multi-axis refactor (Z/X/C), PIO Z stepper driver
- [x] `firmware/test/` — full unit test suite (11 targets), host-compiled with mocked Pico SDK
- [ ] `src/protocol.c` — new JSON command parsers (arm, feed, jog, disengage, feed_hold, resume, reset_fault) + new status fields
- [ ] Hardware bring-up (see `docs/els-bring-up.md`)

## Key Hardware Decisions

- Spindle encoder: Optical, 1000+ PPR, quadrature on GP2/GP3, index on GP4
- X-axis: Heidenhain LS 486C glass scale (20 µm period, distance-coded ref marks, RS-422)
  - Quadrature A/B on GP5/GP6, reference mark on GP7
  - RS-422 receiver: YL-128 (MAX490) module × 3, one per signal pair
  - Short bench leads: connect Ua1+/Ua2+/Ua0+ directly via 1kΩ/2kΩ divider to GPIO (passive, no IC)
  - YL-128 VCC: 3.3V works for bench test (out of spec); 5V + dividers for production
- Z-axis: Glass scale or magnetic encoder (quadrature on GP20/GP21) + CL57T closed-loop stepper (step/dir on GP8/GP9)
- E-stop: GP14 (active low, HW pull-up)
- Physical buttons: GP15 (engage), GP16 (feed hold), GP17 (cycle start)

## Firmware Conventions

- Dual-core: Core 0 = real-time control loop (~20µs), Core 1 = USB comms + housekeeping
- PIO 0: encoder/scale decode (SM 0: spindle, SM 1: X scale, SM 2: Z scale), PIO 1: stepper pulse generation
- Config stored in Pico W flash (wear-leveled), runtime-overridable via serial
- JSON protocol for all Pico W ↔ Android communication
- Onboard LED controlled via CYW43 driver (WL_GPIO0), not GP25
- Safety: E-stop is hardware path (independent of firmware), watchdog on Core 0

## Android Conventions

- Min SDK API 26+, landscape orientation locked
- USB serial via `usb-serial-for-android` library
- Jetpack Compose UI, MVVM with ViewModels + StateFlow
- Dark background, high-contrast digits for workshop visibility

## Webapp (Development Companion)

- Go + HTML/JS web app that replicates the Android DRO display in a browser
- Connects to Pico W via USB serial, bridges to browser via WebSocket
- Run: `cd webapp && go run ./cmd/superdro-web/ -sim` (simulated) or `go run ./cmd/superdro-web/` (auto-detect serial)
- Dark workshop theme matching Android app colors

## Build

- Firmware: CMake with Pico SDK, target `pico_w` (`arm-none-eabi-gcc`)
- Android: Gradle with Kotlin/Compose
- Webapp: `cd webapp && go build ./cmd/superdro-web/`

## Testing

- **Firmware unit tests**: `cd firmware/test && make test` (host-compiled with mocked Pico SDK)
- **Android unit tests**: `cd android && ./gradlew test` (JUnit for Protocol + model logic)
- **E2E tests**: `cd tests/e2e && make test` (simulated serial via pipes + Python validator)
- **Integration tests**: `docs/integration-test-checklist.md` (requires hardware)
- **CI**: GitHub Actions (`.github/workflows/ci.yml`) — firmware tests, cross-compile, Android tests + APK build
- **Docker**: `Dockerfile.firmware` and `Dockerfile.android` for reproducible builds
