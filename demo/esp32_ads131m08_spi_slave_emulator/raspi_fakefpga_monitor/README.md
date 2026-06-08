# FakeFPGA Raspberry Monitor

Pantalla nativa SDL2 para monitorear el segundo ESP32, `FakeFPGA`.

No usa navegador ni servidor. Lee telemetria `FPGA key=value` por USB serial y
muestra calculos/estado del procesador de control.

## Build

```bash
cd /home/pi/zboardProject/demo/esp32_ads131m08_spi_slave_emulator/raspi_fakefpga_monitor
make
```

## Ejecutar

```bash
./fakefpga_monitor --serial /dev/ttyUSB1
```

Si no pasas `--serial`, intenta autodetectar `/dev/ttyUSB0..7` y
`/dev/ttyACM0..7`.

## UI

- Frecuencia estimada por FakeFPGA.
- RMS VA/VB/VC e IA/IB/IC resumido.
- P, Q, PF.
- Desbalance de voltaje.
- Field PWM.
- Estado RUN/AUTO/FAULT.
- Frames ADS leidos y frames malos.
- Boton `EXIT` tactil arriba a la derecha.
