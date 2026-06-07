# ZedBoard Zynq-7000 Soft Starter Digital Twin

Entorno Docker y RTL Verilog para simular un control sincrono de angulo de disparo tipo soft starter/dimmer antes de migrarlo a una ZedBoard Zynq-7000 real.

El objetivo es validar el comportamiento temporal como en un osciloscopio digital: cruce por cero, contador de fase, setpoint de angulo y pulso de disparo hacia un futuro driver SCR.

## Arquitectura

```text
project/
  rtl/
    sync_generator.v
    phase_counter.v
    control_angle.v
    top.v
  tb/
    tb_top.v
  sim/
    run_sim.sh
    soft_starter.gtkw
  Dockerfile
  Makefile
  README.md
```

## Bloques RTL

- `sync_generator.v`: genera una onda cuadrada de sincronia y un pulso `zero_cross_pulse` al final de cada medio ciclo.
- `phase_counter.v`: reinicia en cada cruce por cero y cuenta de `0` a `180`, representando el angulo electrico del semiciclo.
- `control_angle.v`: mantiene `angle_setpoint` y lo modifica con `BTN_UP`, `BTN_DOWN` y `BTN_RESET`.
- `top.v`: integra los bloques y genera `gate_pulse` cuando `phase_count == angle_setpoint`.

## Simulacion local

Requiere `iverilog`, `vvp`, `verilator`, `gtkwave` y `make`.

```bash
make sim
make wave
```

La simulacion genera:

```text
build/waves.vcd
```

Ese archivo se abre con GTKWave y permite observar:

- `sync_square`
- `zero_cross_pulse`
- `phase_count`
- `angle_setpoint`
- `gate_pulse`
- botones simulados `btn_up`, `btn_down`, `btn_reset`

## Uso con Docker

Construir la imagen:

```bash
make docker-build
```

Ejecutar simulacion dentro del contenedor:

```bash
make docker-sim
```

Entrar al entorno de desarrollo:

```bash
make docker-shell
```

Dentro del contenedor:

```bash
make sim
make lint
```

`make lint` revisa el RTL sintetizable con Verilator. El testbench se valida con `make sim`, porque usa retardos y tareas de simulacion.

## Visualizacion con GTKWave

En Linux con servidor grafico disponible:

```bash
make wave
```

En Windows, tambien puedes abrir `build/waves.vcd` directamente desde GTKWave instalado en el host. El archivo `sim/soft_starter.gtkw` incluye una seleccion inicial de senales para verlo como un osciloscopio digital.

## Comportamiento esperado

El testbench:

1. Libera reset.
2. Deja correr dos semiciclos con angulo medio.
3. Simula `BTN_DOWN` para adelantar el disparo.
4. Simula otro `BTN_DOWN` para adelantarlo mas.
5. Simula `BTN_UP` dos veces para retrasar el disparo.
6. Simula `BTN_RESET` para volver al angulo medio.

En GTKWave se debe ver que `gate_pulse` aparece una vez por semiciclo y se desplaza respecto a `zero_cross_pulse` cuando cambia `angle_setpoint`.

## Camino hacia hardware real

Este proyecto esta pensado como primer paso antes de:

- Sintesis en Vivado.
- Integracion en ZedBoard Zynq-7000.
- Mapeo de botones fisicos a `btn_up`, `btn_down`, `btn_reset`.
- Salida GPIO hacia un driver aislado para SCR/triac.
- Reemplazo de `sync_generator` por una entrada real acondicionada de cruce por cero.

Para hardware industrial real se debe agregar aislamiento, deteccion segura de red, protecciones, temporizacion validada y revision electrica completa.

## Demo ESP32 + Raspberry Pi 5

Tambien se incluye un demo inicial para trabajar sin la placa Zynq fisica:

```text
demo/
  esp32_angle_sim/   firmware PlatformIO/Arduino para simular angulo, Hall y cruce por cero
  raspi_monitor/     monitor Python para configurar, visualizar y guardar telemetria
  raspi_web/         dashboard web local para configuracion y medicion
  esp32_ads131m08_virtual/  ADC virtual SPI tipo ADS131M08
  raspi_ads_scope/          scope web para frames SPI del ADC virtual
```

El demo tambien incluye un modelo ADS131M08 virtual de 8 canales para generar `VA/VB/VC`, `IA/IB/IC`, `VDC`, `TEMP` y codigos crudos `adc0..adc7`. Ver `demo/README.md` para el flujo de prueba.

## Prueba segura en ZedBoard

El repo incluye un wrapper para hardware real:

```text
rtl/hardware_top.v
constraints/zedboard.xdc
scripts/build_vivado.tcl
```

La prueba usa solo botones y LEDs de la ZedBoard:

- `BTNU`: incrementa el angulo.
- `BTND`: decrementa el angulo.
- `BTNC`: reset al angulo medio.
- `LD0`: sincronia simulada.
- `LD1`: pulso de gate estirado para verlo en LED.
- `LD2`: pulso de cruce por cero.
- `LD3..LD7`: bits altos de `angle_setpoint`.

Desde PowerShell, con Vivado instalado:

```powershell
& "C:\AMDDesignTools\2025.2\Vivado\bin\vivado.bat" -mode batch -source scripts\build_vivado.tcl
```

El bitstream queda en:

```text
build_vivado/zboard_soft_starter.runs/impl_1/hardware_top.bit
```

Para cargarlo en la tarjeta:

1. Conecta la ZedBoard por USB/JTAG.
2. Abre Vivado.
3. Ve a `Open Hardware Manager`.
4. Usa `Open Target` -> `Auto Connect`.
5. Usa `Program Device`.
6. Selecciona `hardware_top.bit`.

Esta prueba no conecta salidas a potencia. Es solo una validacion segura de temporizacion, botones y LEDs.
