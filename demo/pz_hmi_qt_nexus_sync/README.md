# Nexus Sync Qt/QML HMI for PZ7020

Local HDMI HMI target for the PZ7020 StarLite baseline that already boots Linux
and exposes `card0-HDMI-A-1`.

This app is intentionally separate from the existing Raspberry Pi web HMI and
from the PL control logic. It is a migration shell:

```text
PZ PL / FPGA control
  -> AXI-Lite telemetry and commands
PZ PS / Linux backend
  -> provider abstraction, logging, watchdog
Qt/QML HMI
  -> local HDMI kiosk at 1024x600
```

## Current Status

- Qt/QML application skeleton.
- Demo provider for offline UI validation.
- Shared telemetry names aligned with the existing Raspberry HMI.
- No deterministic control logic has been moved into QML.
- No existing PL RTL has been modified.

## Build

The confirmed PZ PYNQ image does not currently expose `qmake` or `qmlscene` on
PATH. Build this app once Qt5 Quick is installed on the PZ image, or cross-build
it with an ARM Qt5 sysroot.

Native build on a Qt-enabled target:

```bash
cd /opt/nexus/pz_hmi_qt_nexus_sync
mkdir -p build
cd build
cmake ..
make -j2
./nexus-sync-hmi --demo
```

Kiosk run:

```bash
QT_QPA_PLATFORM=linuxfb ./nexus-sync-hmi --demo --fullscreen
```

If EGLFS is available:

```bash
QT_QPA_PLATFORM=eglfs ./nexus-sync-hmi --demo --fullscreen
```

## Runtime Modes

| Mode | Purpose |
| --- | --- |
| `--demo` | Generates local simulated measurements and states. |
| `--axi <base>` | Future direct AXI-Lite mmap provider. |
| `--backend <url>` | Future local service provider. |

## Data Ownership

The PL remains the source of truth for:

- acquisition timing
- PLL/frequency/fasors
- synch check
- sequence state
- SCR timing
- fast trips/protections

The HMI only:

- displays state and measurements
- requests high-level commands
- displays interlocks and degraded communication
- logs UI/runtime events

## Files

```text
CMakeLists.txt
src/
  telemetry_model.*
  data_provider.h
  demo_data_provider.*
  backend_facade.*
qml/
  Main.qml
  components/
systemd/
  nexus-sync-hmi.service
```
