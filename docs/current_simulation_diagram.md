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

## Nexus Sync - Planta Simulada En Raspberry

Nueva arquitectura propuesta para madurar el control antes de migrarlo a PZ7020-StarLite:

```mermaid
flowchart LR
  FakeADS["ESP32 FakeADS<br/>senales analogicas simuladas"]
  FakeFPGA["ESP32 FakeFPGA<br/>control independiente"]
  Plant["Raspberry C++<br/>nexus_motor_plant_sim<br/>planta/motor/breaker/campo"]
  HMI["Raspberry HMI<br/>Nexus Sync"]

  FakeADS -- "SPI + DRDY<br/>muestras ADC" --> FakeFPGA
  FakeFPGA -- "GPIO salidas control<br/>RUN, FIELD PWM, SYNC, FAULT" --> Plant
  Plant -- "GPIO feedbacks<br/>breaker, speed, field,<br/>thermal, emergency" --> FakeFPGA
  Plant -- "serial comandos PROFILE" --> FakeADS
  FakeFPGA -- "USB serial telemetria" --> HMI
```

### Mapa Fisico Fase 1

Lineas actuales FakeFPGA -> Raspberry:

| Senal | FakeFPGA ESP32 | Raspberry BCM | Pin fisico Pi |
| --- | ---: | ---: | ---: |
| MOTOR_RUN_CMD | GPIO25 | GPIO5 | 29 |
| FIELD_ENABLE_CMD | GPIO15 | GPIO26 | 37 |
| FIELD_PWM_CMD | GPIO27 | GPIO6 | 31 |
| SYNC_PULSE | GPIO26 | GPIO13 | 33 |
| FAULT_OUT | GPIO33 | GPIO19 | 35 |

Feedbacks Raspberry -> FakeFPGA reservados:

| Senal | Raspberry BCM | Pin fisico Pi | FakeFPGA ESP32 |
| --- | ---: | ---: | ---: |
| BREAKER_CLOSED_FB | GPIO17 | 11 | GPIO13 |
| SPEED_OK_FB | GPIO22 | 15 | GPIO14 |
| FIELD_CURRENT_FB | GPIO23 | 16 | GPIO16 |
| DISCHARGE_CURRENT_FB | GPIO24 | 18 | GPIO17 |
| THERMAL_OK_FB | GPIO25 | 22 | GPIO21 |
| EXCITER_READY_FB | GPIO12 | 32 | GPIO22 |
| LOAD_READY_FB | GPIO16 | 36 | GPIO32 |
| EMERGENCY_OK_FB | GPIO20 | 38 | GPIO34 |
| PLANT_FAULT_FB | GPIO21 | 40 | GPIO35 |

Notas:

- Todos los GPIO son 3.3 V.
- Usar GND comun.
- No reutilizar GPIO4, GPIO5, GPIO18, GPIO19, GPIO23 del FakeFPGA porque pertenecen a SPI/DRDY del FakeADS.
- `FIELD_ENABLE_CMD` es permiso/contactor digital de campo; `FIELD_PWM_CMD` queda como referencia PWM.
- Los feedbacks quedan listos para la siguiente fase, cuando FakeFPGA deje de depender de `processSim` interno.

### Plan Siguiente

1. Compilar y probar `raspi_motor_plant_sim` en Raspberry en modo `--dry-run`.
2. Probar lectura/escritura GPIO sin conectar aun al ESP32.
3. Actualizar FakeFPGA para leer feedbacks fisicos.
4. Reemplazar gradualmente variables internas de `processSim` por feedbacks de planta.
