# ESP32 ADS131M08 SPI Slave Emulator + Raspberry Pi Master Scope

Este demo contiene:

- `esp32_ads131m08_spi_slave_emulator.ino`: ESP32-WROOM-32 como SPI slave que emula streaming ADS131M08.
- `raspi_master_scope.py`: Raspberry Pi como SPI master con osciloscopio web y control Serial del ESP32.

## Conexiones

Ambos lados usan 3.3 V. Comparte GND entre Raspberry Pi y ESP32.

| Señal | ESP32 GPIO | Raspberry Pi GPIO BCM | Pin físico Pi |
|---|---:|---:|---:|
| CS / CE0 | 5 | 8 | 24 |
| SCLK | 18 | 11 | 23 |
| MOSI | 23 | 10 | 19 |
| MISO | 19 | 9 | 21 |
| DRDY | 4 | 4 | 7 |
| GND | GND | GND | 6 |

Para configurar el ESP32 desde la Pi, conecta tambien el USB del ESP32 a la Raspberry Pi.
El script detecta `/dev/ttyUSB*` o `/dev/ttyACM*`, o puedes pasar `--serial-port`.

## Preparar Raspberry Pi

Habilita SPI:

```bash
sudo raspi-config
```

Luego `Interface Options` -> `SPI` -> `Enable`.

Instala dependencias:

```bash
cd /home/pi/zboardProject/demo/esp32_ads131m08_spi_slave_emulator
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
```

## Ejecutar

```bash
python raspi_master_scope.py --spi-bus 0 --spi-device 0 --spi-hz 10000000 --word-bits 24 --sample-rate 4000 --http-port 8091
```

Abre en un navegador:

```text
http://<ip-de-la-raspberry>:8091
```

## Comandos enviados al ESP32

La UI web manda comandos Serial compatibles con el `.ino`:

```text
BITS 16|24|32
RATE 1000..32000
SPI 5000000|10000000
SPIMODE 0|1|2|3
CRC ON|OFF
MODE CONSTANT|COUNTER|SINE|TRIANGLE|RANDOM
CH <0..7> <MODE> [valor]
START
STOP
CONFIG
```

## Notas de rendimiento

Python en Raspberry Pi puede graficar y medir bien a tasas moderadas. A 16 kSPS o 32 kSPS,
el SPI puede seguir funcionando, pero la UI web decima naturalmente porque solo conserva una
ventana de muestras y refresca por HTTP. Para pruebas de timing fino usa un analizador logico
en `DRDY`, `CS`, `SCLK` y `MISO`.
