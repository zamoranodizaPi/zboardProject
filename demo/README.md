# Motor Angle Control Demo

Primer demo para probar el flujo de control por angulo sin la placa Zynq fisica.

```text
Raspberry Pi 5  <--- USB serial --->  ESP32
                                      |
                                      +--- GPIO: zero_cross, hall_a/b/c, index, fault, gate_window
```

## Componentes

- `esp32_angle_sim/`: firmware PlatformIO/Arduino para simular angulo electrico, cruce por cero, Hall y estados.
- `raspi_monitor/`: herramienta Python para configurar el ESP32, visualizar telemetria y guardar CSV.
- `raspi_web/`: dashboard web local para configurar y medir desde el navegador.
- `esp32_ads131m08_virtual/`: ESP32 como ADC virtual por SPI, parecido al ADS131M08.
- `raspi_ads_scope/`: scope web que lee frames SPI binarios del ADC virtual.

## Flujo recomendado

1. Carga `demo/esp32_angle_sim` al ESP32 desde PlatformIO.
2. Conecta el ESP32 por USB a la Raspberry Pi 5.
3. Ejecuta el monitor:

```bash
cd demo/raspi_monitor
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
python monitor.py --port /dev/ttyUSB0 --speed 30 --log logs/demo.csv
```

En Windows, el puerto suele ser `COM3`, `COM4`, etc.

Para abrir la interfaz web en la Raspberry:

```bash
cd /home/pi/zboardProject/demo/raspi_web
python3 web_dashboard.py --port /dev/ttyUSB0 --host 0.0.0.0 --http-port 8080
```

Luego abre:

```text
http://192.168.1.120:8080
```

## Protocolo serial

Los comandos son texto ASCII terminado en nueva linea:

```text
HELP
ENABLE 1
SPEED 30
ACCEL 120
ANGLE 90
FAULT 0
STATUS
STREAM 10
```

El ESP32 responde con lineas `TEL ...` que la Raspberry parsea para mostrar y guardar datos.

Para ver la forma de onda recortada en la interfaz se usa modo demo lento:

```text
LINEHZ 1
STREAM 20
```

## Nivel ADS131M08 virtual

El simulador tambien genera 8 canales como si fueran muestras escaladas desde un ADC multicanal:

```text
CH0 / va    tension fase A
CH1 / vb    tension fase B
CH2 / vc    tension fase C
CH3 / ia    corriente fase A
CH4 / ib    corriente fase B
CH5 / ic    corriente fase C
CH6 / vdc   bus DC simulado
CH7 / temp  temperatura simulada
```

La telemetria incluye valores de ingenieria (`va`, `ia`, etc.) y codigos crudos firmados de 24 bits (`adc0`..`adc7`). Esto no emula todavia el protocolo SPI real del ADS131M08; es una capa de simulacion para construir configuracion, medicion, tendencias y registro en la Raspberry antes de conectar hardware de adquisicion real.

Ademas se publican `vma`, `vmb` y `vmc`: voltajes simulados que le llegarian al motor despues del recorte por SCR. Estos se calculan desde `va/vb/vc`, `ENABLE`, `FAULT` y el angulo `ANGLE`.
