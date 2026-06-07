# Current Simulation Diagram

Estado del demo actual para simular el frente de medicion/control antes de tener la tarjeta Zynq fisica.

## Arquitectura

```mermaid
flowchart LR
  subgraph ESP32["ESP32 - ADS131M08 virtual"]
    model["Modelo electrico simulado<br/>VIN senoide<br/>VMOT recorte SCR<br/>ILOAD carga<br/>VDC bus DC<br/>THETA angulo<br/>GATE pulso SCR"]
    frame["Frame SPI tipo ADC<br/>32 bytes<br/>8 canales signed 24-bit<br/>sync + status + checksum"]
    config_rx["Config por SPI/MOSI<br/>FS, LINEHZ, ANGLE,<br/>GATEDEG, VPEAK, IPEAK,<br/>VDC, NOISE"]
    model --> frame
    config_rx --> model
  end

  subgraph RPI["Raspberry Pi 5"]
    spi_master["SPI master<br/>/dev/spidev0.0<br/>500 S/s objetivo"]
    decoder["Decoder ADS virtual<br/>valida sync/checksum<br/>escala unidades reales"]
    web["Web dashboard<br/>http://192.168.1.120:8090"]
    chromium["Chromium kiosk<br/>pantalla local Raspberry"]
    spi_master --> decoder
    decoder --> web
    web --> chromium
    spi_master -- "trama config" --> config_rx
  end

  frame -- "MISO: muestras ADC virtuales" --> spi_master
```

## Cableado SPI

```mermaid
flowchart LR
  RPI_SCLK["Raspberry GPIO11 / pin 23<br/>SCLK"] --> ESP_SCLK["ESP32 GPIO18<br/>SCLK"]
  RPI_MOSI["Raspberry GPIO10 / pin 19<br/>MOSI"] --> ESP_MOSI["ESP32 GPIO23<br/>MOSI"]
  ESP_MISO["ESP32 GPIO19<br/>MISO"] --> RPI_MISO["Raspberry GPIO9 / pin 21<br/>MISO"]
  RPI_CE0["Raspberry GPIO8 / pin 24<br/>CE0"] --> ESP_CS["ESP32 GPIO5<br/>CS"]
  RPI_GND["Raspberry GND"] --- ESP_GND["ESP32 GND"]
```

## Flujo De Senales

```mermaid
sequenceDiagram
  participant UI as Dashboard Raspberry
  participant SPI as SPI Master Raspberry
  participant ESP as ESP32 ADS virtual
  participant Model as Modelo SCR

  UI->>SPI: Arranca scope 500 S/s, 10 Hz, gate 30 deg
  loop Cada muestra
    SPI->>ESP: MOSI config frame
    ESP->>Model: Aplica FS, frecuencia, angulo, gate
    Model->>Model: Calcula theta, VIN, VMOT, ILOAD, GATE
    ESP-->>SPI: MISO frame ADC virtual 8 canales
    SPI->>UI: VIN, VMOT, ILOAD, VDC, GATE, THETA, TEMP, FAULT
  end
```

## Canales Simulados

| Canal | Nombre | Significado |
| --- | --- | --- |
| CH0 | VIN | Voltaje senoidal de entrada antes del SCR |
| CH1 | VMOT | Voltaje que llega al motor/carga despues del disparo SCR |
| CH2 | ILOAD | Corriente simulada de carga |
| CH3 | VDC | Bus DC simulado |
| CH4 | GATE | Canal analogico auxiliar de gate |
| CH5 | THETA | Angulo electrico 0..360 deg |
| CH6 | TEMP | Temperatura simulada |
| CH7 | FAULT | Falla simulada |

## Comportamiento Esperado

Con la configuracion actual:

- La entrada `VIN` debe verse como una senoide de aproximadamente `+/-170 V`.
- El pulso `GATE` aparece despues del cruce por cero, en el angulo configurado.
- `VMOT` queda en cero antes del disparo y sigue la senoide despues del disparo, simulando el recorte por SCR.
- A `10 Hz` y `500 S/s`, el dashboard tiene unas 50 muestras por ciclo, suficiente para ver claramente la forma de onda sin saturar la Raspberry.
- El estado `0x0005` significa `enabled` y `gate activo` cuando coincide el pulso.

## Version Actual

El firmware Arduino correcto debe mostrar:

```cpp
// Required firmware baseline: commit 73a74ff
```

