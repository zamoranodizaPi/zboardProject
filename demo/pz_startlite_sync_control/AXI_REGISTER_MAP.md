# Nexus Sync PL AXI Register Map

Base address: assigned by Vivado address editor.

All registers are 32-bit little-endian AXI-Lite.

## Control

| Offset | Name | R/W | Bits |
| --- | --- | --- | --- |
| `0x00` | `ID` | R | `0x4E53594E` (`NSYN`) |
| `0x04` | `VERSION` | R | `0x00010000` |
| `0x08` | `CONTROL` | R/W | bit0 `START_PULSE`, bit1 `STOP_PULSE`, bit2 `RESET_PULSE` |
| `0x0C` | `STATUS` | R | bit0..3 `ctrl_state`, bit4 `freq_locked`, bit12 `fault_out`, bit13 `motor_run`, bit14 `field_enable`, bit15 `frame_valid`, bit16 `frame_bad` |

`CONTROL` command bits are pulse semantics in PL. The PS may write `1`, then `0`.

## Acquisition

| Offset | Name | R/W | Description |
| --- | --- | --- | --- |
| `0x10` | `FRAMES_GOOD` | R | Accepted FakeADS SPI frames |
| `0x14` | `FRAMES_BAD` | R | Rejected FakeADS SPI frames |
| `0x18` | `FREQ_MHZ` | R | Frequency in mHz, e.g. `60000` = 60.000 Hz |
| `0x1C` | `ADS_STATUS` | R | Last ADS status word |
| `0x20` | `CH0_CH1` | R | CH0 in bits 31:16, CH1 in bits 15:0 |
| `0x24` | `CH2_CH3` | R | CH2 in bits 31:16, CH3 in bits 15:0 |
| `0x28` | `CH4_CH5` | R | CH4 in bits 31:16, CH5 in bits 15:0 |
| `0x2C` | `CH6_CH7` | R | CH6 in bits 31:16, CH7 in bits 15:0 |
| `0x30` | `FIELD` | R | bits 9:0 `field_duty` |

## State Encoding

| Value | State |
| --- | --- |
| `0` | `IDLE` |
| `1` | `READY` |
| `2` | `STARTING` |
| `3` | `ACCEL` |
| `4` | `FIELD` |
| `5` | `VERIFY` |
| `6` | `RUNNING` |
| `7` | `FAULT` |
| `8` | `LOCKOUT` |

## Integration Notes

- The SPI acquisition and the synchronous motor sequence remain in PL.
- PS/Linux reads this map for HDMI/HMI and Ethernet services.
- PS/Linux writes only operator commands and configuration; it must not close the fast control loop.
