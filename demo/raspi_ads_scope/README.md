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

El scope configura el ESP32 por SPI/MOSI en cada lectura. Si tambien existe USB serial,
manda los mismos parametros por serial como respaldo:

```text
ENABLE 1
FAULT 0
LINEHZ <line-hz>
FS <sample-rate>
ANGLE <angle-deg>
GATEDEG <gate-deg>
```

## Ejecutar

```bash
python ads_scope.py --spi-bus 0 --spi-device 0 --sample-rate 500 --line-hz 10 --gate-deg 30 --http-port 8090
```

Abrir:

```text
http://192.168.1.120:8090
```
