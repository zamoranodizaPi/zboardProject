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
LINEHZ <hz>
VPEAK <volts_peak>
IPEAK <amps_peak>
VDC <volts>
TEMP <deg_c>
PFDEG <deg>
NOISE <fraction>
STATUS
```

Ejemplo:

```text
ENABLE 1
SPEED 60
ANGLE 90
LINEHZ 1
STREAM 20
```

## Telemetria

El firmware emite lineas como:

```text
TEL t_ms=1234 en=1 fault=0 theta=45.20 speed=60.00 set=90 sector=0 hall=100 gate=0 zc=0 idx=0
```

`hall` se imprime en orden `ABC`, el mismo orden de los pines `hall_a`, `hall_b`, `hall_c`.

La misma linea tambien agrega el modelo ADS virtual:

```text
va vb vc vma vmb vmc ia ib ic vdc temp adc0 adc1 adc2 adc3 adc4 adc5 adc6 adc7
```

Mapeo inicial:

```text
adc0 va   tension fase A
adc1 vb   tension fase B
adc2 vc   tension fase C
vma       tension fase A aplicada al motor despues del SCR
vmb       tension fase B aplicada al motor despues del SCR
vmc       tension fase C aplicada al motor despues del SCR
adc3 ia   corriente fase A
adc4 ib   corriente fase B
adc5 ic   corriente fase C
adc6 vdc  bus DC
adc7 temp temperatura
```
