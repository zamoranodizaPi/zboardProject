# ESP32 ADS131M08 Virtual

Firmware ESP32 para simular un ADC ADS131M08 por SPI.

El ESP32 funciona como **SPI slave** y la Raspberry Pi como **SPI master**. El objetivo es acercarnos al flujo real del ADS131M08: frames binarios, canales signed 24-bit y modo SPI 1.

## Cableado SPI

Todos los niveles son 3.3 V.

```text
Raspberry GPIO 11 SCLK  pin 23  -> ESP32 GPIO 18 SCLK
Raspberry GPIO 10 MOSI  pin 19  -> ESP32 GPIO 23 MOSI
Raspberry GPIO 9  MISO  pin 21  -> ESP32 GPIO 19 MISO
Raspberry GPIO 8  CE0   pin 24  -> ESP32 GPIO 5  CS
Raspberry GND           pin 6   -> ESP32 GND
```

USB queda conectado para alimentacion, carga de firmware y comandos seriales.

## Carga de firmware

PlatformIO:

```text
demo/esp32_ads131m08_virtual
```

Arduino IDE:

```text
demo/esp32_ads131m08_virtual/arduino/esp32_ads131m08_virtual/esp32_ads131m08_virtual.ino
```

El sketch Arduino debe mostrar arriba el comentario:

```text
Required firmware baseline: commit 73a74ff
```

## SPI

```text
Mode: 1 (CPOL=0, CPHA=1)
Frame: 32 bytes
```

Frame:

```text
0      0xA5
1      0x5A
2..3   sequence uint16, big-endian
4..5   status uint16, big-endian
6..29  ch0..ch7 signed 24-bit, big-endian
30..31 checksum uint16, sum bytes 0..29
```

Canales:

```text
CH0 vin       tension de entrada
CH1 vmot      tension recortada hacia motor/carga
CH2 iload     corriente simulada
CH3 vdc       bus DC
CH4 gate      pulso de disparo
CH5 theta     angulo de linea escalado
CH6 temp      temperatura
CH7 fault     estado/falla
```

## Comandos seriales

```text
HELP
ENABLE 1|0
FAULT 1|0
ANGLE 0..180
GATEDEG 1..45
LINEHZ <hz>
FS <samples_per_s>
VPEAK <volts_peak>
IPEAK <amps_peak>
VDC <volts>
TEMP <deg_c>
NOISE <fraction>
STATUS
```

Modo recomendado inicial:

```text
ENABLE 1
LINEHZ 60
FS 3000
ANGLE 90
GATEDEG 15
```

`FS` define el avance interno de fase del ADC virtual. Debe coincidir con el `--sample-rate` del scope Raspberry para que la senoide sea estable.

`GATEDEG` ensancha el pulso de gate para que el scope SPI lo capture de forma consistente. A 60 Hz, 15 grados equivalen aproximadamente a 694 us.
