# Nexus Sync Raspberry Motor Plant Simulator

Aplicativo nativo C++ para simular la planta fisica del motor sincrono desde la Raspberry Pi.

La idea es aislar el control: el FakeFPGA lee entradas/salidas como si estuviera conectado a un proceso real, mientras esta aplicacion simula el motor, breaker, campo, carga, fallas y feedback digital. El FakeADS sigue generando las senales analogicas a partir de perfiles/estados.

## Fase 1 - Contrato I/O

### Cableado existente que NO se debe tocar

FakeADS a FakeFPGA por SPI:

| Funcion | FakeADS ESP32 | FakeFPGA ESP32 | Nota |
| --- | ---: | ---: | --- |
| CS | GPIO5 | GPIO5 | SPI |
| SCLK | GPIO18 | GPIO18 | SPI |
| MOSI | GPIO23 | GPIO23 | SPI master hacia ADS |
| MISO | GPIO19 | GPIO19 | SPI ADS hacia master |
| DRDY | GPIO4 | GPIO4 | muestra lista |
| GND | GND | GND | tierra comun |

Estos pines quedan reservados. No usarlos para la planta simulada.

### FakeFPGA -> Raspberry, salidas de control actuales

Estas salidas ya existen en el firmware FakeFPGA actual.

| Senal | FakeFPGA ESP32 GPIO | Raspberry BCM | Pin fisico Pi | Direccion | Uso en planta |
| --- | ---: | ---: | ---: | --- | --- |
| `MOTOR_RUN_CMD` | GPIO25 | GPIO5 | 29 | ESP32 -> Pi | orden/estado de marcha |
| `FIELD_ENABLE_CMD` | GPIO15 | GPIO26 | 37 | ESP32 -> Pi | permiso/contactor de campo |
| `FIELD_PWM_CMD` | GPIO27 | GPIO6 | 31 | ESP32 -> Pi | referencia PWM de excitacion |
| `SYNC_PULSE` | GPIO26 | GPIO13 | 33 | ESP32 -> Pi | pulso de referencia/sync |
| `FAULT_OUT` | GPIO33 | GPIO19 | 35 | ESP32 -> Pi | falla activa del controlador |

### Raspberry -> FakeFPGA, feedbacks digitales propuestos

Estos pines quedan listos para la Fase 3, cuando el FakeFPGA empiece a leer feedback fisico en lugar de variables internas.

| Senal | Raspberry BCM | Pin fisico Pi | FakeFPGA ESP32 GPIO | Direccion | Significado |
| --- | ---: | ---: | ---: | --- | --- |
| `BREAKER_CLOSED_FB` | GPIO17 | 11 | GPIO13 | Pi -> ESP32 | breaker realmente cerrado |
| `SPEED_OK_FB` | GPIO22 | 15 | GPIO14 | Pi -> ESP32 | velocidad cerca de sincronismo |
| `FIELD_CURRENT_FB` | GPIO23 | 16 | GPIO16 | Pi -> ESP32 | corriente de campo presente |
| `DISCHARGE_CURRENT_FB` | GPIO24 | 18 | GPIO17 | Pi -> ESP32 | corriente de descarga presente |
| `THERMAL_OK_FB` | GPIO25 | 22 | GPIO21 | Pi -> ESP32 | permiso termico |
| `EXCITER_READY_FB` | GPIO12 | 32 | GPIO22 | Pi -> ESP32 | excitador disponible |
| `LOAD_READY_FB` | GPIO16 | 36 | GPIO32 | Pi -> ESP32 | carga/proceso listo |
| `EMERGENCY_OK_FB` | GPIO20 | 38 | GPIO34 | Pi -> ESP32 | cadena de emergencia OK |
| `PLANT_FAULT_FB` | GPIO21 | 40 | GPIO35 | Pi -> ESP32 | falla externa de planta |

Notas:

- GPIO34/GPIO35 del ESP32 son solo entrada y no tienen pull-up/pull-down interno. La Raspberry los maneja como salida activa.
- Todos los niveles son 3.3 V. No conectar 5 V.
- Usar GND comun entre Raspberry y FakeFPGA.
- Recomendado: resistor serie de 330 ohm a 1 kohm en cada linea durante pruebas.
- Arrancar primero el ESP32 y luego el simulador de planta, o mantener las salidas Pi en estado bajo hasta que el ESP32 termine de bootear.

### Raspberry -> FakeADS, feedback analogico por serial

La planta usa dos niveles de control hacia FakeADS:

1. `PROFILE` para cambios gruesos de estado.
2. `PLANT` para drive analogico fino a baja tasa.

| Estado planta | Comando FakeADS |
| --- | --- |
| detenido | `PROFILE NO_SIGNAL` |
| acelerando | `PROFILE START_PROFILE` |
| corriendo estable | `PROFILE GRID_NORMAL` |
| pullout/falla dinamica | `PROFILE PULLOUT` |

Contrato `PLANT` actual:

```text
PLANT <speed_pct> <slip_hz> <current_scale> <field_a> <field_v> <discharge_a> <load_pct> <pullout_risk>
```

La planta lo envia cada 200 ms. FakeADS usa esos valores para:

| Variable planta | Efecto en FakeADS |
| --- | --- |
| `speed_pct` / `slip_hz` | condicion de arranque y acople |
| `stator_current_scale` | amplitud de IA/IB/IC |
| `field_current` / `field_voltage` | mejora de FP, tension/campo y componente VAN |
| `discharge_current` | componente IN durante arranque/paro |
| `load_pct` | angulo de corriente y torque/carga |
| `pullout_risk` | corrientes altas, desbalance y mayor desfase |

## Modelo de excitador / rectificador SCR

La planta ahora incluye un modelo logico del excitador regulado sin agregar pines fisicos nuevos. El objetivo es que el sistema ya tenga las senales que aparecen en la arquitectura electrica del Synchapp/SR-SVR antes de decidir que saldra por cable y que quedara en simulacion.

Separacion interna actual:

```text
FakeFPGA / controlador
  FIELD_ENABLE, FIELD_PWM, secuencia, fallas
        |
        v
ExciterBoard
  RF3, puente SCR, gates G1..G6, conduccion K1..K6,
  crowbar, bleed, descarga y transductores DC
        |
        v
MotorPlant
  velocidad, slip, carga, pullout, sincronismo
        |
        v
FakeADS
  mediciones analogicas que consume el control
```

Por ahora `ExciterBoard` y `MotorPlant` viven dentro del mismo binario `nexus_motor_plant_sim`, pero el limite de responsabilidades ya esta separado en codigo. Esto permite migrar despues el excitador a otra tarjeta/proceso sin reescribir la logica de motor.

Bloques modelados:

| Bloque | Variables publicadas | Nota |
| --- | --- | --- |
| RF3 logic | `rf3_enabled`, `pf_reg_disabled`, `rectifier_ready` | Habilita disparo cuando el campo esta permitido y la planta esta lista |
| Puente SCR 6 pulsos | `g1..g6`, `g1_exec..g6_exec`, `k1..k6`, `scr_firing_deg`, `scr_angle_deg` | `g*` son pulsos de gate; `g*_exec` descuenta fallas; `k*` representa conduccion natural |
| Bus DC rectificado | `dc_bus_voltage`, `rectifier_avg_v`, `rectifier_ripple_v`, `rectifier_inst_v` | Promedio por angulo de disparo, ripple de 6 pulsos e instantanea del puente |
| Transductor voltaje campo | `field_voltage_xdcr` | Separado de la variable de proceso `field_voltage` |
| Transductor corriente campo | `field_current_xdcr` | Separado de `field_current` |
| Transductor descarga | `discharge_current_xdcr` | Separado de `discharge_current` |
| Descarga / bleed / crowbar | `discharge_active`, `bleed_active`, `crowbar_active`, `sk10..sk13` | Ramas auxiliares del campo |
| Senal ajuste FP | `pf_signal` | Senal normalizada 0..1 para regulacion futura |

Mejoras actuales del modelo:

| Area | Cambio |
| --- | --- |
| Puente SCR | seleccion de sector activo de 60 grados, gates `G1..G6`, conduccion `K1..K6`, fallas open/short/gate y overlap dependiente de corriente |
| Bus DC | mezcla de tension instantanea del puente, promedio controlado por angulo de disparo, filtro de bus y ripple de 6 pulsos |
| Motor | aceleracion no lineal por torque disponible, torque de carga, inercia y torque sincronizante cuando aparece campo |
| Diagnostico | publica `load_pct`, `stator_current_scale`, `motor_torque_pu` y `load_torque_pu` |

El campo del rotor usa un modelo RL simple:

```text
di/dt = (Vrect - Icampo * Rcampo) / Lcampo
```

Cuando se deshabilita RF3, se conmuta a las ramas de descarga, bleed o crowbar con resistencias separadas. Los escenarios `GATE_FAIL`, `SCR_OPEN` y `SCR_SHORT` permiten probar perdida de disparo, SCR abierto y SCR en corto sin agregar cableado fisico todavia.

Prioridad para cableado fisico futuro:

1. Directo desde controlador: `G1..G6` por hardware dedicado de pulso/SCR gate.
2. Directo desde controlador: `FIELD_ENABLE`, `FIELD_PWM`, `FAULT_OUT`, `SYNC_PULSE`.
3. Por expansor I/O lento: `SK10..SK13`, `CROWBAR`, `BLEED`, `DISCHARGE`, `RF3_ENABLE`.
4. Por FakeADS/ADC simulado: transductores `field_voltage_xdcr`, `field_current_xdcr`, `discharge_current_xdcr`.

Cuando separemos cableado, el primer cambio fisico recomendado sera sacar `G1..G6` desde el controlador hacia la tarjeta de excitacion. Los estados lentos pueden esperar y viajar como palabra de estado compacta o por telemetria.

### Contrato FakeFPGA -> ExciterBoard

Estado actual sin recablear:

| Senal | Origen | Destino actual | Destino futuro |
| --- | --- | --- | --- |
| `scrcmd_g1..scrcmd_g6` | FakeFPGA | Telemetria/HMI | Pines fisicos hacia driver SCR |
| `scrcmd_fire` | FakeFPGA | Telemetria/HMI | Parametro de diagnostico |
| `FIELD_ENABLE` | FakeFPGA GPIO15 | Raspberry BCM26 | Contactor/permisivo excitador |
| `FIELD_PWM` | FakeFPGA GPIO27 | Raspberry BCM6 | Referencia regulador excitacion |
| `FWT` | FakeFPGA logico | Telemetria/HMI | Auxiliar freewheel/field timing |
| `DST` | FakeFPGA logico | Telemetria/HMI | Auxiliar descarga |

El FakeFPGA ya publica `scrcmd_*` como comando logico de disparo. El `ExciterBoard` todavia genera sus propios gates internos porque no hay cable fisico `G1..G6`. La HMI muestra ambos bancos:

- `FPGA Gate Cmd`: lo que el controlador quiere disparar.
- `Exciter Gates`: lo que la tarjeta simulada esta ejecutando.

Cuando se conecten `G1..G6`, el siguiente cambio sera hacer que `ExciterBoard` use los gates de entrada en lugar de su generador interno.

## Oscilografia COMTRADE

El backend Nexus Sync genera registros COMTRADE en la Raspberry en:

```text
/tmp/nexus_comtrade
```

Cada registro produce:

| Archivo | Uso |
| --- | --- |
| `.cfg` | configuracion COMTRADE |
| `.dat` | muestras ASCII COMTRADE |
| `.json` | copia para visualizacion rapida en HMI |

Eventos que disparan registro automatico:

| Evento | Trigger |
| --- | --- |
| arranque | transicion de controlador a `STARTING` |
| paro | `RUN_CMD` pasa de 1 a 0 |
| falla | `FAULT` pasa de 0 a 1 |
| comandos | `STARTSEQ`, `STOPSEQ`, `ACK`, `RESET`, `SCENARIO` |

Canales analogicos principales:

```text
VA, VB, VC, IA, IB, IC,
FREQ, PF, P, Q,
SPEED, SLIP,
FIELD_I, FIELD_V, DISCH_I, DC_BUS
```

Canales digitales principales:

```text
RUN_CMD, FIELD_ENABLE, RF3_ENABLED,
G1_CMD..G6_CMD,
G1_EXEC..G6_EXEC,
BREAKER, SPEED_OK, FIELD_FB, DISCH_FB,
CROWBAR, FAULT, PLANT_FAULT, FWT, DST
```

La HMI incluye la vista `Oscilografia`, desde donde se pueden listar registros, visualizar curvas y descargar `CFG/DAT`.

### Oscilografia electrica de alta tasa

Tambien existe un generador COMTRADE de alta tasa para simulacion electrica:

| Parametro | Valor actual |
| --- | --- |
| tasa | 8000 muestras/s |
| ventana | 3 s pre-trigger + 5 s post-trigger |
| duracion total | 8 s |
| muestras por evento | 64000 |
| fuente actual | modelo sintetico basado en telemetria/planta |
| fuente futura | stream crudo FakeADS/FakeFPGA |

Disparos actuales:

| Evento | Momento de armado |
| --- | --- |
| `STARTSEQ` | antes de enviar el comando al FakeFPGA |
| `STOPSEQ` | antes de enviar el comando al FakeFPGA |
| `FAULT` | cuando `fault` pasa de 0 a 1 |
| escenarios de falla | antes de enviar el escenario |
| happy path | comando `HAPPY_PATH` desde la HMI |

Importante: en esta fase no se mantiene una ventana continua de 8 kS/s en RAM. Al evento se genera el registro simulado completo y solo se guarda una version decimada para visualizacion HMI. El `.dat` conserva la resolucion completa.

### Happy Path de aceptacion

La HMI incluye el boton `HAPPY PATH START / STOP` en Operacion. Este comando:

1. Fuerza la planta a `SCENARIO NORMAL`.
2. Envia `RESET`, `ACK` y `STARTSEQ` al FakeFPGA.
3. Genera una oscilografia COMTRADE `HAPPY_PATH`.
4. Programa un `STOPSEQ` limpio despues de la marcha estable.

El registro `HAPPY_PATH` esta pensado como prueba base sin fallas simuladas. Contiene arranque, aceleracion, corriente de descarga, aplicacion de campo, RF3/SCR gates, bus DC, sincronismo, operacion estable y paro.

Parametros actuales:

| Parametro | Valor |
| --- | --- |
| tasa | 8000 muestras/s |
| duracion | 30 s |
| muestras | 240000 |
| descarga | ZIP con `.cfg` + `.dat` |

## Fase 2 - Aplicacion C++

Compilar en Raspberry:

```bash
cd /home/pi/zboardProject/demo/esp32_ads131m08_spi_slave_emulator/raspi_motor_plant_sim
make
```

Modo simulacion sin GPIO:

```bash
./nexus_motor_plant_sim --dry-run --ads-serial /dev/ttyUSB0
```

Modo cableado:

```bash
sudo ./nexus_motor_plant_sim --ads-serial /dev/ttyUSB0
```

Opciones utiles:

```bash
./nexus_motor_plant_sim --help
./nexus_motor_plant_sim --dry-run --auto-run
sudo ./nexus_motor_plant_sim --pin-test
./nexus_motor_plant_sim --tick-ms 5 --ads-serial /dev/ttyUSB0
```

Separacion de campo:

- `FIELD_ENABLE_CMD` es la senal digital que habilita/aplica campo.
- `FIELD_PWM_CMD` queda como referencia de excitacion y ya no se usa como permiso digital.
- Si aun no esta conectado `FIELD_ENABLE_CMD`, se puede probar en modo compatibilidad con `--field-enable-bcm -1`.

Telemetria y comandos locales:

| Archivo | Uso |
| --- | --- |
| `/tmp/nexus_motor_plant_state.json` | telemetria JSON de planta |
| `/tmp/nexus_motor_plant_cmd` | FIFO de comandos |

Comandos al FIFO:

```bash
echo "SCENARIO NORMAL" | sudo tee /tmp/nexus_motor_plant_cmd
echo "SCENARIO NO_DISCHARGE" | sudo tee /tmp/nexus_motor_plant_cmd
echo "SCENARIO NO_FIELD" | sudo tee /tmp/nexus_motor_plant_cmd
echo "SCENARIO THERMAL_TRIP" | sudo tee /tmp/nexus_motor_plant_cmd
echo "SCENARIO PULLOUT" | sudo tee /tmp/nexus_motor_plant_cmd
```

Test de pines:

```bash
sudo ./nexus_motor_plant_sim --pin-test
```

Durante el test, revisar en la telemetria del FakeFPGA que cambien:

```text
pfb_breaker
pfb_speed
pfb_field
pfb_discharge
pfb_thermal
pfb_exciter
pfb_load
pfb_emergency
pfb_fault
```

El mismo test imprime las salidas fisicas que ve desde FakeFPGA:

```text
FPGA_OUT motor_run=...
FPGA_OUT field_enable=...
FPGA_OUT field_pwm=...
FPGA_OUT sync=...
FPGA_OUT fault=...
```

## Plan siguiente

Despues de Fase 1/2:

1. Actualizar FakeFPGA para leer `*_FB` fisicos.
2. Sustituir `processSim` interno por entradas de planta.
3. Agregar telemetria de planta al backend/HMI.
4. Agregar comandos FakeADS analogicos finos, no solo `PROFILE`.
