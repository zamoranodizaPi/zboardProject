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
