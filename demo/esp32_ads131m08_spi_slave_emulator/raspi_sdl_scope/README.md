# ADS131M08 SDL Instrument Scope

Aplicacion C++ nativa para Raspberry Pi OS Bookworm, pensada para modo headless
o appliance sin escritorio. Usa SDL2 sobre KMS/DRM, `spidev` para adquisicion y
`termios` para configurar el ESP32 por USB serial.

No usa navegador, Electron, Python, Qt ni servidor web.

## Arquitectura

- Thread 1: adquisicion SPI por `/dev/spidev0.0`, sincronizada con DRDY GPIO4.
- Validacion de `STATUS_WORD` y rechazo suave de spikes para ocultar frames corruptos.
- Thread 2: procesamiento, ventana temporal y trigger edge.
- Thread 3: render SDL2 fullscreen a 60 FPS con double buffering del renderer.
- Ring buffer prealocado de 65536 muestras.
- Dos buffers de display prealocados para evitar locks pesados entre processing y render.
- Afinidad CPU best-effort para adquisicion, procesamiento y render.

## UI

- Fondo negro tipo instrumento.
- Graticule con divisiones mayores y menores.
- Ejes centrales resaltados.
- 8 canales maximos.
- Colores tipo osciloscopio:
  - VA amarillo
  - VB cyan
  - VC magenta
  - VAN verde
  - IA naranja
  - IB azul
  - IC violeta
  - IN gris
- Overlay superior: RUN/HOLD, TRIG'D/AUTO/WAIT, sample rate, time/div, V/div, FPS.
- Trigger level con linea horizontal.

## Controles

```text
q / ESC       salir
SPACE         RUN / HOLD
t             trigger auto / normal
e             trigger rising / falling
[ / ]         bajar / subir trigger level
+ / -         zoom horizontal time/div
UP / DOWN     escala vertical V/div
TAB           cambiar canal de trigger
1..8          mostrar/ocultar canal
```

## Dependencias Raspberry Pi OS Bookworm Lite

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libsdl2-dev
sudo raspi-config
```

En `raspi-config`, habilita SPI en `Interface Options`.

El usuario que ejecuta la app debe tener acceso a `spi`, `dialout`, `video` y
`render`:

```bash
sudo usermod -aG spi,dialout,video,render pi
```

Reinicia despues de modificar grupos.

## Build

```bash
cd /home/pi/zboardProject/demo/esp32_ads131m08_spi_slave_emulator/raspi_sdl_scope
make
```

## Ejecutar manualmente

Desde una consola local de la Raspberry:

```bash
SDL_VIDEODRIVER=kmsdrm SDL_AUDIODRIVER=dummy ./ads131_scope_sdl \
  --spi-dev /dev/spidev0.0 \
  --serial /dev/ttyUSB0 \
  --spi-hz 5000000 \
  --bits 24 \
  --rate 4000 \
  --channels 8 \
  --drdy-gpio 4
```

Para probar dentro de un escritorio:

```bash
./ads131_scope_sdl --windowed --spi-dev /dev/spidev0.0 --serial /dev/ttyUSB0
```

## Instalar como appliance systemd

```bash
cd /home/pi/zboardProject/demo/esp32_ads131m08_spi_slave_emulator/raspi_sdl_scope
make
sudo make install-service
sudo systemctl enable ads131-scope.service
sudo systemctl start ads131-scope.service
```

Para revisar:

```bash
systemctl status ads131-scope.service
journalctl -u ads131-scope.service -f
```

## Boot directo sin escritorio

```bash
sudo systemctl set-default multi-user.target
sudo systemctl disable --now lightdm 2>/dev/null || true
sudo systemctl disable --now display-manager 2>/dev/null || true
sudo reboot
```

Al arrancar, systemd lanzara la app fullscreen por KMS/DRM. El objetivo practico
es mostrar pantalla en 3 a 5 segundos despues de que el kernel y systemd entregan
los dispositivos `/dev/spidev0.0` y `/dev/ttyUSB0`.

## Notas de rendimiento

- No se reserva memoria en el loop principal de adquisicion.
- El renderer usa SDL accelerated + present vsync.
- La pantalla se limpia completamente por defecto para evitar trazos acumulados.
- El efecto phosphor se puede activar explicitamente con `--phosphor`.
- Para mejor estabilidad, el ESP32 usa DRDY en modo nivel: activo hasta que termina
  la transaccion SPI.
- Si quieres latencia minima, usa modo Lite sin escritorio y `SDL_VIDEODRIVER=kmsdrm`.
