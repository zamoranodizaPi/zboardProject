# Nexus Sync PZ7020 PL Control

Migracion del control deterministico de Nexus Sync desde FakeFPGA ESP32 hacia la
logica programable de la PZ7020-StarLite.

## Objetivo

```text
FakeADS / ADC real
   -> SPI + DRDY
PZ7020 PL
   -> adquisicion deterministica
   -> desempaque CH0..CH7
   -> mediciones base
   -> FSM de control
   -> salidas rapidas
PZ7020 PS/Linux
   -> HMI, Ethernet, logs, COMTRADE, configuracion
```

Este primer bitstream migra la columna vertebral deterministica:

- SPI master para FakeADS/ADS-like.
- Captura de frame `STATUS + CH0..CH7`, 16 bits.
- Validacion de `STATUS = 0x5xxx` o `0xA751`.
- Frecuencia base por cruce por cero de VA.
- Presencia de senal y ventana de frecuencia.
- Maquina de estados inicial: `IDLE`, `READY`, `STARTING`, `ACCEL`, `FIELD`, `SYNC_VERIFY`, `RUNNING`, `FAULT`.
- PWM de campo.
- Pulso de sincronismo.
- Salidas digitales de control.

La matematica PLL/fasorial completa se migrara en la siguiente fase como bloque
fixed-point dedicado.

## Pinout usado

JM1 Bank35, default 3.3 V. No conectar 5 V a IO. Usar GND comun.

| Funcion | PZ7020 conector | FPGA pin | Direccion | Uso |
| --- | --- | --- | --- | --- |
| `SPI_SCLK` | JM1 pin 5 | H16 | PZ -> FakeADS | reloj SPI |
| `SPI_MOSI` | JM1 pin 7 | H17 | PZ -> FakeADS | dummy MOSI |
| `SPI_MISO` | JM1 pin 9 | E18 | FakeADS -> PZ | datos |
| `SPI_CS_N` | JM1 pin 11 | E19 | PZ -> FakeADS | chip select |
| `DRDY_N` | JM1 pin 13 | G17 | FakeADS -> PZ | data ready activo bajo |
| `MOTOR_RUN` | JM1 pin 15 | G18 | PZ -> externo | marcha motor |
| `FIELD_ENABLE` | JM1 pin 17 | D19 | PZ -> externo | permiso campo |
| `FIELD_PWM` | JM1 pin 19 | D20 | PZ -> externo | referencia excitacion |
| `SYNC_PULSE` | JM1 pin 21 | J18 | PZ -> externo | pulso sincronismo |
| `FAULT_OUT` | JM1 pin 23 | H18 | PZ -> externo | falla activa |
| `START_CMD` | JM1 pin 25 | K17 | entrada | comando start |
| `STOP_CMD` | JM1 pin 27 | K18 | entrada | comando stop |
| `RESET_CMD` | JM1 pin 29 | L16 | entrada | reset/ack |
| `THERMAL_OK` | JM1 pin 31 | L17 | entrada | permisivo termico |
| `GND` | JM1 pin 3, 4, 33, 34, 35 o 36 | GND | comun | tierra |

Pines de alimentacion del header:

```text
JM1 pin 1 = 5V
JM1 pin 2 = 3.3V
```

No usar `JM1 pin 2` como GND.

## LEDs

| LED | Significado |
| --- | --- |
| LED1 | frame ADS valido / actividad |
| LED2 | falla o status ADS invalido |

## Build

```powershell
cd C:\Users\zamor\Documents\zboardProject\demo\pz_startlite_sync_control
powershell -ExecutionPolicy Bypass -File .\build_and_program.ps1
```

## Fases siguientes

1. Agregar telemetria PL->PS via AXI-Lite.
2. Migrar DPLL/SOGI fixed-point para frecuencia/fase.
3. Calcular RMS/fasores/potencias en PL.
4. Migrar disparo SCR de 6 pulsos con angulo controlado.
5. Levantar Linux/PetaLinux para HMI HDMI/Ethernet.
