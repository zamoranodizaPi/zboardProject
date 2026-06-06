# ESP32 Angle Simulator

Firmware PlatformIO/Arduino para simular senales de entrada de un control por angulo.

## Pines por defecto

Todos son salidas a 3.3 V.

```text
GPIO 18  zero_cross_pulse
GPIO 19  hall_a
GPIO 21  hall_b
GPIO 22  hall_c
GPIO 23  index_pulse
GPIO 25  fault_out
GPIO 26  gate_window
```

Cuando conectes a una FPGA/Zynq:

- Comparte GND entre ESP32 y la tarjeta.
- No apliques 5 V a entradas de FPGA.
- Usa buffers/aislamiento si sales del banco de pruebas de baja tension.

## Comandos serial

```text
HELP
ENABLE 1|0
SPEED <deg_e_per_s>
ACCEL <deg_e_per_s2>
ANGLE <0..180>
FAULT 1|0
STREAM <period_ms>
STATUS
```

Ejemplo:

```text
ENABLE 1
SPEED 60
ANGLE 90
STREAM 20
```

## Telemetria

El firmware emite lineas como:

```text
TEL t_ms=1234 en=1 fault=0 theta=45.20 speed=60.00 set=90 sector=0 hall=100 gate=0 zc=0 idx=0
```

`hall` se imprime en orden `ABC`, el mismo orden de los pines `hall_a`, `hall_b`, `hall_c`.
