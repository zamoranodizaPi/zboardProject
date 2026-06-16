# Nexus Sync PZ7020 HMI Migration Architecture

## Goal

Move the local operator HMI from Raspberry Pi to the PZ7020 StarLite HDMI output
without moving deterministic motor-control logic into Linux or QML.

## Confirmed Baseline

- Board: PZ7020 StarLite / Zynq-7000.
- Linux image: vendor PYNQ/PetaLinux HDMI image.
- HDMI: working on 1024x600 display.
- UART shell: `xilinx@pynq`.
- DRM connector exists: `/sys/class/drm/card0-HDMI-A-1`.
- Qt is not installed in the current image yet.
- `/dev/fb0` and `/dev/dri/card0` exist.

## Ownership

### PL / FPGA

PL remains the source of truth for deterministic behavior:

- FakeADS/ADS SPI timing.
- Frame validation.
- PLL, frequency, RMS, phasors, power.
- Synchronous motor sequence.
- SCR timing and gate logic.
- Fast trips and interlocks.
- AXI-Lite register map exposed to PS.

### PS / Linux

Linux owns non-deterministic supervision:

- Local HMI process.
- Optional backend service.
- Logging and event history.
- Persistent configuration.
- Ethernet access.
- COMTRADE export.
- Watchdog and service restart.

### HMI

The HMI may:

- Display measurements and states.
- Send high-level commands: START, STOP, ACK, RESET.
- Display interlocks and degraded communication.

The HMI must not:

- Fire SCR gates directly.
- Close timing loops.
- Bypass interlocks.
- Calculate protection trips as the source of truth.

## Incremental Migration

1. Freeze the vendor HDMI image as the golden baseline.
2. Add the Qt/QML HMI in demo mode.
3. Add a PS-side provider reading the PL AXI-Lite register map.
4. Replace demo data with PL telemetry.
5. Add FakeADS diagnostics and link health.
6. Add Fake Motor/Fake Exciter interface stubs.
7. Add persistent config and operator logs.
8. Add systemd service in kiosk mode.

## Current New App

Path:

```text
demo/pz_hmi_qt_nexus_sync
```

It provides:

- `TelemetryModel`: UI-facing data model.
- `DataProvider`: abstract measurement/control data source.
- `DemoDataProvider`: offline simulation.
- `ExciterInterface`: future Fake/Real exciter abstraction.
- QML 1024x600 main screen.
- `nexus-sync-hmi.service` systemd unit.

## Provider Plan

```text
DataProvider
  DemoDataProvider        offline UI validation
  AxiLiteProvider         mmap /dev/mem or UIO for PL registers
  LocalBackendProvider    optional IPC to local Linux service
```

## AXI Integration

The existing PL map is documented in:

```text
demo/pz_startlite_sync_control/AXI_REGISTER_MAP.md
```

The first AXI provider should map these fields:

| UI Field | AXI Source |
| --- | --- |
| `ctrl` | `STATUS[3:0]` |
| `fault` | `STATUS[12]` |
| `run` | `STATUS[13]` |
| `field_enable` | `STATUS[14]` |
| `frame_valid` | `STATUS[15]` |
| `frame_bad` | `STATUS[16]` |
| `frames_good` | `FRAMES_GOOD` |
| `frames_bad` | `FRAMES_BAD` |
| `f` | `FREQ_MHZ / 1000.0` |
| `ch0..ch7` | packed channel registers |
| `field_duty` | `FIELD[9:0]` |

## Build Note

The current working HDMI image does not include Qt on PATH. Before running the
QML app locally, install Qt5 Quick packages or build a Qt-enabled rootfs. Until
then, the app is ready as source and can be cross-built once the target sysroot
is fixed.
