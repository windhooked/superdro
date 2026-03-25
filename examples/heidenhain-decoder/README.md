# Heidenhain Glass Scale Decoder — Raspberry Pi Pico (PIO)

## Overview

PIO-based x4 quadrature decoder for Heidenhain TTL (RS-422) glass scales with
**distance-coded reference mark** absolute position computation.

- **Scale signal period:** 4 µm (configurable for 20 µm / 40 µm)
- **Decode mode:** x4 (all edges)
- **Resolution:** 1 µm per count
- **Output:** UART0 at 115200 baud (GP0 TX, GP1 RX)
- **Absolute position:** Established after traversing just 2 reference marks (~20 mm)

## How Absolute Position Works

Heidenhain "C" models (e.g. LF 481C, LS 487C, LB 382C) have **distance-coded
reference marks** — multiple index marks spaced at varying intervals according to
a mathematical algorithm.

### Startup sequence:
1. Power on → incremental counting starts immediately (relative to power-on position)
2. Traverse the scale in any direction
3. First reference mark detected → position recorded, status shows `[1/2 ref]`
4. Continue traversing (~20 mm max for LF series)
5. Second reference mark detected → **absolute position computed!**
6. From this point on, both incremental AND absolute positions are reported

### The formula (from Heidenhain documentation):
```
B  = 2 × MRR − N
P1 = ((|B| − sgn(B) − 1) / 2) × N  +  ((sgn(B) − sgn(D)) / 2) × |MRR|

Where:
  P1  = absolute position of first ref mark (in signal periods from scale origin)
  MRR = signal periods between the two traversed reference marks
  N   = nominal increment (see table)
  D   = direction of traverse (+1 right, −1 left)
```

### Scale parameters:
| Series | Signal Period | N (nominal incr.) | Max traverse to lock |
|--------|--------------|-------------------|---------------------|
| LF (C) | 4 µm         | 5000              | 20 mm               |
| LS (C) | 20 µm        | 1000              | 20 mm               |
| LB (C) | 40 µm        | 2000              | 80 mm               |

Change `SIGNAL_PERIOD_UM` and `NOMINAL_INCREMENT_N` in `main.c` for your model.

## Wiring

**RS-422 line receiver required** (AM26LS32, SN75175, or MAX489).

| Heidenhain Signal | RS-422 Receiver | Pico GPIO |
|---|---|---|
| Ua1 / Ua1_inv (Ch A) | Channel A out | GP2 |
| Ua2 / Ua2_inv (Ch B) | Channel B out | GP3 |
| Ua0 / Ua0_inv (Index) | Channel C out | GP4 |
| UART TX | — | GP0 → your device RX |
| UART RX (optional) | — | GP1 ← your device TX |

> Channels A and B **must** be on consecutive GPIOs.

## Build

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Flash `heidenhain_glass_scale.uf2` via BOOTSEL.

## Serial Commands

| Command | Action |
|---|---|
| `z` | Zero counter + reset absolute reference |
| `r` | Reset reference state only (keep counting) |
| `a` | Query current absolute position |

## Output Format

```
INC:+12.345 mm D:+5 ABS:+412.345 mm          ← normal operation (absolute known)
INC:+0.023 mm D:+23 ABS:--- [1/2 ref]         ← one ref mark seen, need one more
INC:+0.000 mm D:+0 ABS:--- [no ref]           ← no ref marks seen yet

>> REF#1 @ count=+92000 (dir=+1) [need 1 more ref mark]
>> REF#2 @ count=+112400 (dir=+1) ABS=+412345 um (+412.345 mm)
   [DISTANCE-CODE DECODE]
   delta=+20400 counts, MRR=5100 periods, D=+1
   P1=103086 periods => 1st ref @ +412344 um
   offset=+320344 (abs = inc + +320344)
   *** ABSOLUTE POSITION ESTABLISHED ***
```

## Architecture

- **PIO state machine:** Handles all x4 quadrature edge detection at ~12.5 MHz rate with zero CPU load
- **GPIO interrupt:** Captures index (reference mark) pulses and records incremental count at each
- **Distance-code algorithm:** Runs on CPU when second ref mark is detected — computes P1 from MRR and establishes absolute offset
- **Main loop:** Periodic UART output + serial command handling

## Files

| File | Purpose |
|---|---|
| `quadrature_encoder.pio` | PIO assembly — jump table x4 decoder |
| `main.c` | Distance-coded ref mark algorithm, UART output, commands |
| `CMakeLists.txt` | Pico SDK build config |
