# FakeADS Protocol

This document freezes the current FakeADS behavior used by Nexus Sync and the
PZ migration.

## Role

The ESP32 FakeADS emulates an ADS131M08-like streaming ADC. It is intentionally
focused on sample streaming and DRDY synchronization, not full ADS131M08
register behavior.

## Electrical

- Logic: 3.3 V.
- Common GND required.
- No 5 V on ESP32 or PZ IO.

## Signals

| Signal | FakeADS ESP32 | Direction |
| --- | ---: | --- |
| `CS_N` | GPIO 5 | master -> FakeADS |
| `SCLK` | GPIO 18 | master -> FakeADS |
| `MOSI` | GPIO 23 | master -> FakeADS |
| `MISO` | GPIO 19 | FakeADS -> master |
| `DRDY_N` | GPIO 4 | FakeADS -> master |

## Frame

The current PZ PL path expects 16-bit words:

```text
STATUS
CH0
CH1
CH2
CH3
CH4
CH5
CH6
CH7
```

CRC is not required in the current PZ integration.

## Word Format

- `INPUT_TYPE`: signed 16-bit channel samples.
- Endianness on the wire: MSB first by SPI word.
- `STATUS`: valid values accepted by the PL are `0x5xxx` or `0xA751`.

## Channel Map

| Channel | Signal | Nominal |
| ---: | --- | --- |
| CH0 | VA | 120 V RMS equivalent |
| CH1 | VB | 120 V RMS equivalent, -120 deg |
| CH2 | VC | 120 V RMS equivalent, +120 deg |
| CH3 | VAN / aux voltage | 120 V equivalent |
| CH4 | IA | 5 A RMS equivalent |
| CH5 | IB | 5 A RMS equivalent, -120 deg from IA |
| CH6 | IC | 5 A RMS equivalent, +120 deg from IA |
| CH7 | IN / aux current | neutral or scenario signal |

## Rates

The control migration targets:

- preferred: 16 kS/s
- accepted test rate: 8 kS/s
- word length: 16 bit

At 16 kS/s and 60 Hz:

```text
samples_per_cycle = 16000 / 60 = 266.67
sample_period = 62.5 us
electrical_degrees_per_sample = 360 / 266.67 = 1.35 deg
```

## Serial Configuration Commands

The ESP32 FakeADS supports commands over USB serial:

```text
BITS 16|24|32
RATE 1000..32000
SPI 5000000|10000000
SPIMODE 0|1|2|3
CRC ON|OFF
MODE CONSTANT|COUNTER|SINE|TRIANGLE|RANDOM
CH <0..7> <MODE> [value]
START
STOP
CONFIG
```

## Future ADS Real Transition

The HMI must not depend on this protocol directly. It should receive normalized
measurements from:

```text
MeasurementProvider
  FakeAdsProvider
  RealAdsProvider
```

The PL/PS boundary should hide ADC transport details from QML.
