# ESP32 Frequency DPLL

Modulo de calculo de frecuencia para medidor de energia/proteccion electrica.

## Objetivo

- Entrada: `int16_t`.
- Resolucion ADC esperada: `16 bits`.
- Sample rate fijo recomendado: `16000 SPS`.
- Rango de frecuencia: `45..65 Hz`.
- Frecuencia nominal: `60 Hz`.
- Actualizacion: cada `32` muestras, aproximadamente `2 ms`.
- Respuesta: menor o igual a 1 ciclo para escalones moderados.
- Sin FFT y sin ventanas largas.

## Archivos

- `frequency_pll.h`
- `frequency_pll.cpp`
- `example.ino`

## API

```cpp
FrequencyPllConfig cfg;
cfg.sampleRate = 16000;
cfg.nominalFreqHz = 60.0f;
initFrequency(cfg);

pushSamples(samples, count);

float f = getFrequency();
float rocof = getROCOF();
float phase = getPhase();
bool locked = isLocked();
FrequencyPllSnapshot s = getFrequencySnapshot();
```

`pushSamples()` esta pensado para llamarse desde una tarea de adquisicion que ya
lee el ADC o desde un consumidor de buffer circular existente. El modulo no
implementa adquisicion ADC.

## Pipeline

```text
int16_t samples
  -> DC removal
  -> amplitude normalization
  -> SOGI quadrature generator
  -> synchronous phase detector
  -> PI loop filter
  -> NCO
  -> frequency / ROCOF / phase
```

## Matematica breve

### DC removal

```text
dc[n] = dc[n-1] + alpha * (x[n] - dc[n-1])
y[n]  = x[n] - dc[n]
```

`alpha` se calcula desde un corte cercano a `5 Hz`.

### SOGI

Se usa un `Second Order Generalized Integrator` discreto para generar dos
senales en cuadratura:

```text
I = componente en fase
Q = componente en cuadratura
```

La frecuencia central del SOGI se actualiza con el NCO del PLL para que el filtro
siga la red.

### Phase detector

El detector usa una rotacion sincrona:

```text
vq = -I*sin(theta) + Q*cos(theta)
```

Cuando el PLL esta alineado, `vq -> 0`.

### Loop filter

Filtro PI:

```text
omega_correction += Ki * error * dt
omega = omega_nominal + omega_correction + Kp * error
```

Luego:

```text
theta += omega * dt
frequency = omega / (2*pi)
```

## Parametros ajustables

| Parametro | Default | Efecto |
|---|---:|---|
| `pllBandwidthHz` | `5 Hz` | Mayor = responde mas rapido, menor = mas estable |
| `damping` | `0.707` | Amortiguamiento del lazo |
| `dcCutoffHz` | `5 Hz` | Remocion de offset DC |
| `amplitudeTauMs` | `8 ms` | Suavizado de normalizacion |
| `outputTauMs` | `8 ms` | Suavizado de frecuencia publicada |
| `updateSamples` | `32` | Periodo de publicacion interna |

## FreeRTOS

El modulo crea:

- `TaskFrequency`: prioridad alta. Consume bloques desde una queue.
- `TaskOutput`: prioridad normal. Publica telemetria cada `500 ms`.

La tarea de adquisicion del sistema debe llamar `pushSamples()`.

## Telemetria

Cada `500 ms`:

```text
FPLL freq=60.0000 rocof=0.000 phase=... lock=1 cpu=... loop_us=... samples=... drops=...
```

## Validacion sugerida

- `60 Hz`.
- `59 Hz`.
- `61 Hz`.
- Escalon `60 -> 57 Hz`.
- Ruido `±2%`.
- Tercer armonico `3%`.

Con adquisicion limpia, el objetivo es error menor a `0.02 Hz` y lock menor a
un ciclo. Para estabilidad `±0.01 Hz`, baja `pllBandwidthHz` a `2..3 Hz` o sube
`outputTauMs`, aceptando mas retardo.

## Notas de rendimiento

- No usa `malloc` en runtime.
- Queue fija de bloques.
- Bloque recomendado: `32` muestras.
- RAM esperada: muy por debajo de `50 KB`.
- CPU objetivo: menor a `10%` en ESP32 para un canal.
