# Raspberry Pi Web Dashboard

Interfaz web local para configurar el ESP32 angle simulator y visualizar mediciones del modelo ADS virtual.

## Uso

```bash
cd /home/pi/zboardProject/demo/raspi_web
python3 web_dashboard.py --port /dev/ttyUSB0 --host 0.0.0.0 --http-port 8080
```

Abrir desde otra maquina en la misma red:

```text
http://192.168.1.120:8080
```

## Controles

- Enable / fault.
- Referencia de velocidad.
- Angulo de disparo.
- Frecuencia de linea.
- Pico de tension/corriente.
- Bus DC.
- Desfase de corriente.
- Ruido de medicion.

La app usa solo biblioteca estandar de Python y `pyserial`.
