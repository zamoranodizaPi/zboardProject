#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <stdint.h>

static constexpr uint16_t FPLL_ADC_BITS = 16;
static constexpr uint32_t FPLL_SAMPLE_RATE = 16000;
static constexpr float FPLL_NOMINAL_FREQ_HZ = 60.0f;
static constexpr float FPLL_MIN_FREQ_HZ = 45.0f;
static constexpr float FPLL_MAX_FREQ_HZ = 65.0f;
static constexpr uint16_t FPLL_UPDATE_SAMPLES = 32;
static constexpr uint16_t FPLL_BLOCK_SAMPLES = 32;

struct FrequencyPllConfig {
  uint32_t sampleRate = FPLL_SAMPLE_RATE;
  float nominalFreqHz = FPLL_NOMINAL_FREQ_HZ;
  float minFreqHz = FPLL_MIN_FREQ_HZ;
  float maxFreqHz = FPLL_MAX_FREQ_HZ;
  float pllBandwidthHz = 5.0f;
  float damping = 0.707f;
  float dcCutoffHz = 5.0f;
  float amplitudeTauMs = 8.0f;
  float outputTauMs = 8.0f;
  uint16_t updateSamples = FPLL_UPDATE_SAMPLES;
  UBaseType_t taskFrequencyPriority = configMAX_PRIORITIES - 2;
  UBaseType_t taskOutputPriority = 1;
  BaseType_t taskCore = 1;
};

struct FrequencyPllSnapshot {
  float frequencyHz = FPLL_NOMINAL_FREQ_HZ;
  float rocofHzPerSec = 0.0f;
  float phaseRad = 0.0f;
  float amplitude = 0.0f;
  bool locked = false;
  float cpuPercent = 0.0f;
  uint32_t loopTimeUs = 0;
  uint32_t droppedBlocks = 0;
  uint32_t processedSamples = 0;
};

bool initFrequency(const FrequencyPllConfig &config = FrequencyPllConfig{});
bool pushSamples(const int16_t *samples, uint16_t count);
float getFrequency();
float getROCOF();
float getPhase();
bool isLocked();
FrequencyPllSnapshot getFrequencySnapshot();
void resetFrequencyPll();
