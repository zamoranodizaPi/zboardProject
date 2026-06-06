# Raspberry Pi Monitor

Herramienta Python para configurar el ESP32 angle simulator y leer su telemetria.

## Instalacion

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
```

## Uso

```bash
python monitor.py --port /dev/ttyUSB0 --speed 30 --angle 90 --log logs/demo.csv
```

En Windows:

```powershell
python monitor.py --port COM3 --speed 30 --angle 90 --log logs\demo.csv
```

Comandos interactivos:

```text
s <speed>     cambia velocidad electrica en deg/s
a <angle>     cambia angulo de disparo 0..180
e <0|1>       enable
f <0|1>       fault
r <accel>     aceleracion en deg/s2
lhz <hz>      frecuencia de linea simulada
vp <volts>    pico de tension fase-neutro simulado
ip <amps>     pico de corriente simulado
vdc <volts>   bus DC simulado
pf <deg>      desfase corriente/tension
n <fraction>  ruido de medicion, por ejemplo 0.002
q             salir
```
