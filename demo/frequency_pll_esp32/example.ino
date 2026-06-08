/*
  Frequency PLL example for ESP32 Arduino.

  This example does not implement ADC acquisition. It generates synthetic int16_t
  samples at 16 kS/s and feeds them through pushSamples(), the same API a real
  ADC circular-buffer task would call.
*/

#include "frequency_pll.h"

#include <math.h>

static constexpr float kTwoPi = 6.28318530717958647692f;
static constexpr uint16_t kBlock = 32;

static TaskHandle_t taskAcquireHandle = nullptr;
static int16_t sampleBlock[kBlock];

static float simPhase = 0.0f;
static uint32_t simSamples = 0;
static uint32_t rngState = 0x12345678u;

uint32_t xorshift32() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}

float testFrequency() {
  const float t = static_cast<float>(simSamples) / static_cast<float>(FPLL_SAMPLE_RATE);
  if (t < 3.0f) return 60.0f;
  if (t < 6.0f) return 59.0f;
  if (t < 9.0f) return 61.0f;
  if (t < 12.0f) return 60.0f;
  return 57.0f;
}

void taskAcquire(void *) {
  TickType_t wake = xTaskGetTickCount();
  const TickType_t blockTicks = pdMS_TO_TICKS(2);

  for (;;) {
    for (uint16_t i = 0; i < kBlock; ++i) {
      const float f = testFrequency();
      simPhase += kTwoPi * f / static_cast<float>(FPLL_SAMPLE_RATE);
      if (simPhase > kTwoPi) simPhase -= kTwoPi;

      const float noise = ((static_cast<int32_t>(xorshift32() & 0xFFFF) - 32768) / 32768.0f) * 0.02f;
      const float harmonic3 = 0.03f * sinf(3.0f * simPhase + 0.4f);
      const float signal = 0.72f * sinf(simPhase) + harmonic3 + noise;
      sampleBlock[i] = static_cast<int16_t>(constrain(signal, -0.98f, 0.98f) * 32767.0f);
      simSamples++;
    }
    pushSamples(sampleBlock, kBlock);
    vTaskDelayUntil(&wake, blockTicks);
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 1200) {
    delay(1);
  }

  FrequencyPllConfig cfg;
  cfg.sampleRate = 16000;
  cfg.nominalFreqHz = 60.0f;
  cfg.minFreqHz = 45.0f;
  cfg.maxFreqHz = 65.0f;
  cfg.pllBandwidthHz = 5.0f;
  cfg.damping = 0.707f;
  cfg.updateSamples = 32;

  if (!initFrequency(cfg)) {
    Serial.println("Frequency PLL init failed");
    for (;;) delay(1000);
  }

  xTaskCreatePinnedToCore(taskAcquire, "TaskAcquire", 3072, nullptr,
                          configMAX_PRIORITIES - 1, &taskAcquireHandle, 1);
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 500) {
    last = millis();
    FrequencyPllSnapshot s = getFrequencySnapshot();
    Serial.print("APP freq=");
    Serial.print(s.frequencyHz, 4);
    Serial.print(" rocof=");
    Serial.print(s.rocofHzPerSec, 3);
    Serial.print(" phase=");
    Serial.print(s.phaseRad, 4);
    Serial.print(" lock=");
    Serial.println(s.locked ? 1 : 0);
  }
  delay(10);
}
