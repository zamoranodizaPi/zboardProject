# FakeFPGA ESP32

Segundo ESP32 del demo. Lee al `FakeADS` por SPI, calcula mediciones basicas y
genera salidas iniciales para control de motor sincrono.

## Topologia inicial

```text
FakeADS ESP32  -- SPI + DRDY -->  FakeFPGA ESP32  -- USB serial --> Raspberry Pi
```

La Raspberry no debe ser master del mismo bus SPI al mismo tiempo que FakeFPGA.
Para ver el osciloscopio actual, usa Raspberry como master. Para probar la cadena
FakeFPGA, conecta FakeFPGA como master del FakeADS y usa la pantalla
`raspi_fakefpga_monitor`.

## Cableado FakeADS -> FakeFPGA

Los nombres son desde el punto de vista de cada placa.

| FakeADS | FakeFPGA | Funcion |
|---|---:|---|
| GPIO 5 CS | GPIO 5 CS | seleccion SPI |
| GPIO 18 SCLK | GPIO 18 SCLK | reloj SPI generado por FakeFPGA |
| GPIO 19 MISO | GPIO 19 MISO | datos FakeADS -> FakeFPGA |
| GPIO 23 MOSI | GPIO 23 MOSI | comandos/dummy FakeFPGA -> FakeADS |
| GPIO 4 DRDY | GPIO 4 DRDY | muestra lista |
| GND | GND | referencia comun |

## Salidas de control FakeFPGA

| Pin | Funcion |
|---:|---|
| GPIO 25 | MOTOR_RUN digital |
| GPIO 26 | SYNC_PULSE, pulso en cruce por cero de VA |
| GPIO 27 | FIELD_PWM |
| GPIO 33 | FAULT digital |

## Serial

115200 baud. Telemetria cada 100 ms:

```text
FPGA seq=123 fps=4000.0 bad=0 f=60.000 va=0.56 ia=0.38 p=0.42 pf=0.86 field=0.35 run=1 fault=0
```

Comandos:

```text
START
STOP
RUN ON|OFF
AUTO ON|OFF
FIELD 0.0..0.95
VSET 0.05..1.0
CONFIG
STATUS
```
