# PZ7020 Qt HMI Runbook

## Baseline

Use the vendor HDMI image that already shows console on the 7 inch 1024x600
display. Do not merge files from the previous failed PetaLinux 2025.2 attempt.

Minimum boot partition files:

```text
BOOT.BIN
image.ub
```

## Confirm Target

UART:

```bash
115200 8N1
```

Expected:

```text
xilinx@pynq:~$
```

Confirm HDMI connector:

```bash
ls /sys/class/drm
```

Expected:

```text
card0
card0-HDMI-A-1
version
```

## Current Image Capabilities Observed

```text
python3: present
gcc/g++: present
cmake: present
qmake/qmlscene: not present on PATH
/dev/fb0: present
/dev/dri/card0: present
X11/Weston: not running by default
```

## Deploy Source

Copy:

```text
demo/pz_hmi_qt_nexus_sync
```

to:

```text
/opt/nexus/zboardProject/demo/pz_hmi_qt_nexus_sync
```

## Install Qt Runtime

The exact package path depends on whether the PYNQ image has apt sources
enabled. Candidate packages:

```bash
sudo apt update
sudo apt install qtbase5-dev qtdeclarative5-dev qml-module-qtquick-controls2
```

If apt is unavailable or too heavy, use cross-build with an ARM Qt5 sysroot.

## Build

```bash
cd /opt/nexus/zboardProject/demo/pz_hmi_qt_nexus_sync
mkdir -p build
cd build
QT_SELECT=qt5 cmake ..
make -j2 nexus-sync-hmi-lite
```

## Run Embedded Lite Demo

Framebuffer:

```bash
QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0 QT_QPA_FB_DRM=0 ./nexus-sync-hmi-lite --demo --fullscreen
```

## Run With PL/FakeADS Measurements

The HMI does not read FakeADS through Linux `spidev`. FakeADS is wired to PL
pins, then the PL exposes normalized registers through AXI-Lite.

Once the HDMI Vivado project includes the `nexus_sync_control` AXI IP, use the
base address assigned by Vivado:

```bash
QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0 QT_QPA_FB_DRM=0 ./nexus-sync-hmi-lite --axi 0x43C20000 --fullscreen
```

## Install Service

```bash
sudo cp /opt/nexus/zboardProject/demo/pz_hmi_qt_nexus_sync/systemd/nexus-sync-hmi.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable nexus-sync-hmi.service
sudo systemctl start nexus-sync-hmi.service
```

Default service mode is demo:

```text
NEXUS_HMI_ARGS=--demo --fullscreen
```

To switch to PL/FakeADS telemetry after the AXI IP is integrated:

```bash
echo 'NEXUS_HMI_ARGS=--axi 0x43C20000 --fullscreen' | sudo tee /etc/default/nexus-sync-hmi
sudo systemctl restart nexus-sync-hmi.service
```

For debug:

```bash
sudo systemctl disable nexus-sync-hmi.service
sudo systemctl stop nexus-sync-hmi.service
journalctl -u nexus-sync-hmi.service -f
```
