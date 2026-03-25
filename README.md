# SuperDRO

Raspberry Pi Pico W-based lathe controller combining DRO (digital readout), electronic leadscrew (ELS), and conversational turning. 10" Android tablet as the UI.

## Features

- **DRO**: Live X, Z, and spindle RPM display at 1 µm resolution
- **Electronic Leadscrew**: Spindle-synchronized Z-axis threading — metric, imperial, modular, diametral pitch
- **Multi-pass threading**: Absolute spindle-index tracking for repeatable thread passes
- **Configurable**: All machine parameters (encoder PPR, leadscrew pitch, steps/rev, belt ratio) settable at runtime via serial or config screen

## Architecture

| Component | Role |
|---|---|
| Raspberry Pi Pico W | Hard real-time motion control & sensor IO |
| Android tablet (10") | Display, job setup, parameter entry |
| USB Serial (115200 baud, JSON) | Pico W ↔ Android communication |

**Pico W dual-core:**
- Core 0: Real-time control loop (~20 µs period) — ELS step generation, encoder reads, safety
- Core 1: USB serial comms, DRO status reporting (~50 Hz)

**PIO allocation:**

| Block | SM | Function |
|---|---|---|
| PIO 0 | 0 | Spindle quadrature decode |
| PIO 0 | 1 | X-axis scale decode |
| PIO 0 | 2 | Z-axis scale decode |
| PIO 1 | 0 | Z stepper pulse generation |

## Hardware

### Bill of Materials

| Item | Example Part | Est. Cost |
|---|---|---|
| Raspberry Pi Pico W | RP2040 + CYW43439 | $6 |
| Spindle encoder | Omron E6B2-CWZ6C 1000 PPR | $25–50 |
| X-axis glass scale | 200–300 mm, 5 µm, TTL | $30–60 |
| Z-axis glass scale | 500–600 mm, 5 µm, TTL | $40–80 |
| Z stepper | NEMA 23, 2–3 Nm | $25–40 |
| Z stepper driver | CL57T closed-loop | $40–60 |
| Timing belt + pulleys | GT2/HTD 3M, 1:1 ratio | $15–25 |
| 10" Android tablet | USB OTG required | $100–150 |
| E-stop, buttons, PSU, enclosure | | $50–70 |
| **Total** | | **~$330–540** |

### GPIO Pinout

| GPIO | Function |
|---|---|
| GP2 / GP3 | Spindle encoder A/B (PIO 0, SM 0) |
| GP4 | Spindle index pulse |
| GP5 / GP6 | X-axis scale A/B (PIO 0, SM 1) |
| GP8 | Z step (PIO 1, SM 0) |
| GP9 | Z direction |
| GP10 | Z enable |
| GP14 | E-stop (active low, hardware pull-up) |
| GP15 | Engage / disengage |
| GP16 | Feed hold |
| GP17 | Cycle start |
| GP20 / GP21 | Z-axis scale A/B (PIO 0, SM 2) |

## Project Structure

```
superdro/
├── firmware/          # Pico W C firmware (CMake + Pico SDK)
│   ├── src/           # C source files
│   └── pio/           # PIO assembly programs
├── android/           # Kotlin/Compose Android app
├── webapp/            # Go + HTML/JS development companion (USB serial → browser)
├── examples/
│   └── heidenhain-decoder/  # Standalone Heidenhain distance-coded scale decoder
└── docs/              # Wiring diagrams, BOM, protocol docs
```

## Build

### Firmware

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cd firmware && mkdir build && cd build
cmake -DPICO_BOARD=pico_w ..
make -j$(nproc)
```

Or via Docker (reproducible, no local toolchain needed):

```bash
docker build -f Dockerfile.firmware -t superdro-firmware .
docker run --rm -v "$PWD":/src superdro-firmware
```

### Android

```bash
cd android && ./gradlew assembleDebug
```

### Web companion (development tool)

Runs in the browser, bridges USB serial to WebSocket — useful for development without the Android app.

```bash
cd webapp && go run ./cmd/superdro-web/ -sim   # simulated spindle + encoders
cd webapp && go run ./cmd/superdro-web/         # auto-detect USB serial port
```

## Testing

```bash
make test                         # firmware unit tests (host-compiled, no hardware)
cd android && ./gradlew test      # Android unit tests
cd tests/e2e && make test         # E2E tests via simulated serial pipe
```

CI runs all of the above on every push via GitHub Actions.

## Development Phases

| Phase | Status | Goal |
|---|---|---|
| 1 — DRO | Complete (pending hardware integration test) | X, Z, RPM live display |
| 2 — ELS | In progress | Spindle-synchronized Z threading |
| 3 — Conversational | Planned | Canned cycles — requires X stepper |

## License

MIT — see [LICENSE](LICENSE).
