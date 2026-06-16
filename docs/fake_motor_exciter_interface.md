# Fake Motor / Fake Exciter Interface Stub

This document defines the placeholder interface for the future ESP32-based
Fake Motor and Fake Exciter. It does not implement a detailed physical model
yet.

## Purpose

The Fake Motor/Fake Exciter will close the loop around the PZ control by
simulating plant feedback and exciter behavior while keeping deterministic
control in PL.

## Future Inputs From PZ

| Signal | Meaning |
| --- | --- |
| `MOTOR_RUN` | motor contactor / run command |
| `FIELD_ENABLE` | field contactor or RF3 permission |
| `FIELD_PWM` | excitation reference |
| `SCR_G1..SCR_G6` | SCR gate commands when physically exposed |
| `FWT` | freewheel / field winding transfer logic |
| `DST` | discharge / resistor path command |
| `RESET/ACK` | fault reset path |

## Future Feedback To PZ

| Signal | Meaning |
| --- | --- |
| `speed_pct` | rotor speed percent |
| `rotor_angle_deg` | rotor electrical angle |
| `load_pct` | mechanical load |
| `torque_pu` | estimated motor torque |
| `field_current` | field current feedback |
| `field_voltage` | field voltage feedback |
| `discharge_current` | discharge resistor current |
| `exciter_ready` | exciter ready permissive |
| `thermal_ok` | thermal permissive |
| `plant_fault` | plant-side fault |

## Transport

Do not select the final transport yet. Candidate options:

- GPIO for critical interlocks.
- SPI for compact status words.
- UART for debug and early prototype.
- UDP only for lab debug if already available.

## HMI Contract

The HMI consumes normalized fields only:

```text
plant_state
speed_pct
slip_hz
field_current
field_voltage
discharge_current
exciter_status
faults
```

The HMI does not care if they come from:

- demo provider
- Fake Motor/Fake Exciter ESP32
- real exciter
- PL AXI registers

## Initial Mock States

```text
INIT
READY
STARTING
ACCELERATION
FIELD_APPLY
SYNC_VERIFY
RUNNING
FAULT
LOCKOUT
```

## First Implementation Recommendation

Expose one compact status frame first:

```text
uint16 flags
float speed_pct
float field_current
float field_voltage
float discharge_current
float rotor_angle_deg
```

Keep physical pins for timing-critical signals, especially SCR gates and fast
interlocks.
