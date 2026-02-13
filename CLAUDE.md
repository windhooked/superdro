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
├── docs/              # Wiring diagrams, BOM, protocol docs
└── prd.md             # Full PRD with Phase 1 implementation plan
```

## Current Phase

**Phase 1 — DRO**: Position display (X, Z, spindle RPM) on Android tablet. Implementation plan with 12 tasks is in `prd.md` under "Phase 1 Implementation Plan".

### Implementation Status (Phase 1)

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

## Key Hardware Decisions

- Spindle encoder: Optical, 1000+ PPR, quadrature on GP2/GP3, index on GP4
- X-axis: Glass scale or magnetic encoder (quadrature on GP5/GP6)
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

## Build

- Firmware: CMake with Pico SDK, target `pico_w` (`arm-none-eabi-gcc`)
- Android: Gradle with Kotlin/Compose

## Testing

- **Firmware unit tests**: `cd firmware/test && make test` (host-compiled with mocked Pico SDK)
- **Android unit tests**: `cd android && ./gradlew test` (JUnit for Protocol + model logic)
- **E2E tests**: `cd tests/e2e && make test` (simulated serial via pipes + Python validator)
- **Integration tests**: `docs/integration-test-checklist.md` (requires hardware)
- **CI**: GitHub Actions (`.github/workflows/ci.yml`) — firmware tests, cross-compile, Android tests + APK build
- **Docker**: `Dockerfile.firmware` and `Dockerfile.android` for reproducible builds
