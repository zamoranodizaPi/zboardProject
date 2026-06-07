# Raspberry ADS Scope

Dashboard web para leer el ADS131M08 virtual por SPI y graficar bloques de muestras.

## Habilitar SPI en Raspberry

```bash
sudo raspi-config
```

Usar `Interface Options` -> `SPI` -> `Enable`.

Verificar:

```bash
ls /dev/spidev*
```

## Instalar dependencias

```bash
cd /home/pi/zboardProject/demo/raspi_ads_scope
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
```

El scope intenta configurar el ESP32 por USB serial antes de abrir SPI:

```text
ENABLE 1
FAULT 0
LINEHZ 60
FS <sample-rate>
ANGLE 90
GATEDEG 15
```

## Ejecutar

```bash
python ads_scope.py --spi-bus 0 --spi-device 0 --sample-rate 3000 --http-port 8090
```

Abrir:

```text
http://192.168.1.120:8090
```
