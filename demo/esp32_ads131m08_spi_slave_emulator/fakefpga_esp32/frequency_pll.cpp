#include "frequency_pll.h"

#include <math.h>

namespace {

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTwoPi = 6.28318530717958647692f;
static constexpr uint8_t kQueueBlocks = 16;
static constexpr float kInvI16FullScale = 1.0f / 32768.0f;

struct SampleBlock {
  uint16_t count = 0;
  int16_t samples[FPLL_BLOCK_SAMPLES]{};
};

struct PllState {
  FrequencyPllConfig cfg;
  QueueHandle_t queue = nullptr;
  TaskHandle_t frequencyTask = nullptr;
  TaskHandle_t outputTask = nullptr;
  portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
  FrequencyPllSnapshot snap;

  float dt = 1.0f / FPLL_SAMPLE_RATE;
  float dcAlpha = 0.0f;
  float ampAlpha = 0.0f;
  float outAlpha = 0.0f;
  float kp = 0.0f;
  float ki = 0.0f;
  float dc = 0.0f;
  float amp = 0.1f;

  float sogiX1 = 0.0f;
  float sogiX2 = 0.0f;
  float sogiY1 = 0.0f;
  float sogiY2 = 0.0f;
  float sogiQ1 = 0.0f;
  float sogiQ2 = 0.0f;

  float theta = 0.0f;
  float omega = kTwoPi * FPLL_NOMINAL_FREQ_HZ;
  float omegaCorrection = 0.0f;
  float freqOut = FPLL_NOMINAL_FREQ_HZ;
  float prevFreqOut = FPLL_NOMINAL_FREQ_HZ;
  float rocofOut = 0.0f;
  float lockIntegrator = 0.0f;

  uint32_t processed = 0;
  uint32_t dropped = 0;
  uint32_t lastTelemetryMs = 0;
  uint32_t lastTaskUs = 0;
  bool initialized = false;
};

PllState g;

float clampf(float value, float lo, float hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

float wrapPi(float x) {
  while (x > kPi) x -= kTwoPi;
  while (x < -kPi) x += kTwoPi;
  return x;
}

float lowPassAlpha(float cutoffHz, float dt) {
  const float rc = 1.0f / (kTwoPi * cutoffHz);
  return dt / (rc + dt);
}

float tauAlphaMs(float tauMs, float dt) {
  const float tau = tauMs * 0.001f;
  if (tau <= 0.0f) return 1.0f;
  return 1.0f - expf(-dt / tau);
}

void copySnapshotLocked(const FrequencyPllSnapshot &s) {
  portENTER_CRITICAL(&g.lock);
  g.snap = s;
  portEXIT_CRITICAL(&g.lock);
}

FrequencyPllSnapshot readSnapshotLocked() {
  FrequencyPllSnapshot s;
  portENTER_CRITICAL(&g.lock);
  s = g.snap;
  portEXIT_CRITICAL(&g.lock);
  return s;
}

void resetStateOnly() {
  g.dc = 0.0f;
  g.amp = 0.1f;
  g.sogiX1 = 0.0f;
  g.sogiX2 = 0.0f;
  g.sogiY1 = 0.0f;
  g.sogiY2 = 0.0f;
  g.sogiQ1 = 0.0f;
  g.sogiQ2 = 0.0f;
  g.theta = 0.0f;
  g.omegaCorrection = 0.0f;
  g.omega = kTwoPi * g.cfg.nominalFreqHz;
  g.freqOut = g.cfg.nominalFreqHz;
  g.prevFreqOut = g.cfg.nominalFreqHz;
  g.rocofOut = 0.0f;
  g.lockIntegrator = 0.0f;
  g.processed = 0;
  g.dropped = 0;
  g.lastTaskUs = 0;
  g.snap = FrequencyPllSnapshot{};
  g.snap.frequencyHz = g.cfg.nominalFreqHz;
}

void configureDerived() {
  g.dt = 1.0f / static_cast<float>(g.cfg.sampleRate);
  g.dcAlpha = lowPassAlpha(g.cfg.dcCutoffHz, g.dt);
  g.ampAlpha = tauAlphaMs(g.cfg.amplitudeTauMs, g.dt);
  const float updateDt = g.dt * static_cast<float>(g.cfg.updateSamples);
  g.outAlpha = tauAlphaMs(g.cfg.outputTauMs, updateDt);

  const float wn = kTwoPi * g.cfg.pllBandwidthHz;
  g.kp = 2.0f * g.cfg.damping * wn;
  g.ki = wn * wn;
}

void sogiStep(float x, float omega, float *iOut, float *qOut) {
  const float fs = static_cast<float>(g.cfg.sampleRate);
  const float w0 = clampf(omega, kTwoPi * g.cfg.minFreqHz, kTwoPi * g.cfg.maxFreqHz);
  const float k = 1.41421356237f;
  const float osgx = 2.0f * k * w0 * fs;
  const float osgy = w0 * w0;
  const float den = 4.0f * fs * fs + osgx + osgy;

  const float b0 = osgx / den;
  const float b2 = -b0;
  const float a1 = (2.0f * (4.0f * fs * fs - osgy)) / den;
  const float a2 = (osgx - 4.0f * fs * fs - osgy) / den;

  const float qb0 = k * osgy / den;
  const float qb1 = 2.0f * qb0;
  const float qb2 = qb0;

  const float y = b0 * x + b2 * g.sogiX2 + a1 * g.sogiY1 + a2 * g.sogiY2;
  const float q = qb0 * x + qb1 * g.sogiX1 + qb2 * g.sogiX2 + a1 * g.sogiQ1 + a2 * g.sogiQ2;

  g.sogiX2 = g.sogiX1;
  g.sogiX1 = x;
  g.sogiY2 = g.sogiY1;
  g.sogiY1 = y;
  g.sogiQ2 = g.sogiQ1;
  g.sogiQ1 = q;

  *iOut = y;
  *qOut = q;
}

void processSample(int16_t raw, uint32_t *maxLoopUs) {
  const uint32_t startUs = micros();
  const float xRaw = static_cast<float>(raw) * kInvI16FullScale;

  g.dc += g.dcAlpha * (xRaw - g.dc);
  float x = xRaw - g.dc;
  g.amp += g.ampAlpha * (fabsf(x) - g.amp);
  const float norm = x / clampf(g.amp * 1.41421356237f, 0.02f, 1.25f);

  float i = 0.0f;
  float q = 0.0f;
  sogiStep(norm, g.omega, &i, &q);

  const float ncoSin = sinf(g.theta);
  const float ncoCos = cosf(g.theta);
  const float vq = -i * ncoSin + q * ncoCos;
  const float phaseError = clampf(vq, -0.5f, 0.5f);

  g.omegaCorrection += g.ki * phaseError * g.dt;
  const float minCorr = kTwoPi * (g.cfg.minFreqHz - g.cfg.nominalFreqHz);
  const float maxCorr = kTwoPi * (g.cfg.maxFreqHz - g.cfg.nominalFreqHz);
  g.omegaCorrection = clampf(g.omegaCorrection, minCorr, maxCorr);

  g.omega = kTwoPi * g.cfg.nominalFreqHz + g.omegaCorrection + g.kp * phaseError;
  g.omega = clampf(g.omega, kTwoPi * g.cfg.minFreqHz, kTwoPi * g.cfg.maxFreqHz);

  g.theta = wrapPi(g.theta + g.omega * g.dt);
  g.processed++;

  const float errAbs = fabsf(phaseError);
  g.lockIntegrator += 0.02f * (((errAbs < 0.035f) ? 1.0f : 0.0f) - g.lockIntegrator);

  const uint32_t elapsed = micros() - startUs;
  if (elapsed > *maxLoopUs) *maxLoopUs = elapsed;
}

void publishEstimate(uint32_t loopUs) {
  const float rawFreq = g.omega / kTwoPi;
  g.freqOut += g.outAlpha * (rawFreq - g.freqOut);

  const float updateDt = g.dt * static_cast<float>(g.cfg.updateSamples);
  const float rocof = (g.freqOut - g.prevFreqOut) / updateDt;
  g.prevFreqOut = g.freqOut;
  g.rocofOut += 0.25f * (rocof - g.rocofOut);

  FrequencyPllSnapshot s;
  s.frequencyHz = g.freqOut;
  s.rocofHzPerSec = g.rocofOut;
  s.phaseRad = g.theta;
  s.amplitude = g.amp * 1.41421356237f;
  s.locked = g.lockIntegrator > 0.85f && g.freqOut >= g.cfg.minFreqHz && g.freqOut <= g.cfg.maxFreqHz;
  s.loopTimeUs = loopUs;
  s.droppedBlocks = g.dropped;
  s.processedSamples = g.processed;
  s.cpuPercent = (static_cast<float>(loopUs) / (g.dt * 1000000.0f)) * 100.0f;
  copySnapshotLocked(s);
}

void frequencyTask(void *) {
  SampleBlock block;
  uint16_t sinceUpdate = 0;
  uint32_t maxLoopUs = 0;
  for (;;) {
    if (xQueueReceive(g.queue, &block, portMAX_DELAY) != pdTRUE) continue;
    for (uint16_t i = 0; i < block.count; ++i) {
      processSample(block.samples[i], &maxLoopUs);
      sinceUpdate++;
      if (sinceUpdate >= g.cfg.updateSamples) {
        publishEstimate(maxLoopUs);
        sinceUpdate = 0;
        maxLoopUs = 0;
      }
    }
  }
}

void outputTask(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    FrequencyPllSnapshot s = readSnapshotLocked();
    Serial.print(F("FPLL freq=")); Serial.print(s.frequencyHz, 4);
    Serial.print(F(" rocof=")); Serial.print(s.rocofHzPerSec, 3);
    Serial.print(F(" phase=")); Serial.print(s.phaseRad, 4);
    Serial.print(F(" lock=")); Serial.print(s.locked ? 1 : 0);
    Serial.print(F(" cpu=")); Serial.print(s.cpuPercent, 2);
    Serial.print(F(" loop_us=")); Serial.print(s.loopTimeUs);
    Serial.print(F(" samples=")); Serial.print(s.processedSamples);
    Serial.print(F(" drops=")); Serial.println(s.droppedBlocks);
  }
}

}  // namespace

bool initFrequency(const FrequencyPllConfig &config) {
  if (g.initialized) return true;
  g.cfg = config;
  if (g.cfg.sampleRate == 0 || g.cfg.updateSamples == 0) return false;
  configureDerived();
  resetStateOnly();

  g.queue = xQueueCreate(kQueueBlocks, sizeof(SampleBlock));
  if (!g.queue) return false;

  BaseType_t ok = xTaskCreatePinnedToCore(frequencyTask, "TaskFrequency", 4096, nullptr,
                                          g.cfg.taskFrequencyPriority,
                                          &g.frequencyTask, g.cfg.taskCore);
  if (ok != pdPASS) return false;

  ok = xTaskCreatePinnedToCore(outputTask, "TaskOutput", 3072, nullptr,
                               g.cfg.taskOutputPriority, &g.outputTask, g.cfg.taskCore);
  if (ok != pdPASS) return false;

  g.initialized = true;
  return true;
}

bool pushSamples(const int16_t *samples, uint16_t count) {
  if (!g.initialized || !samples || count == 0) return false;

  uint16_t offset = 0;
  bool allQueued = true;
  while (offset < count) {
    SampleBlock block;
    const uint16_t remaining = count - offset;
    block.count = remaining > FPLL_BLOCK_SAMPLES ? FPLL_BLOCK_SAMPLES : remaining;
    for (uint16_t i = 0; i < block.count; ++i) block.samples[i] = samples[offset + i];
    offset += block.count;
    if (xQueueSend(g.queue, &block, 0) != pdTRUE) {
      g.dropped += block.count;
      allQueued = false;
    }
  }
  return allQueued;
}

float getFrequency() {
  return readSnapshotLocked().frequencyHz;
}

float getROCOF() {
  return readSnapshotLocked().rocofHzPerSec;
}

float getPhase() {
  return readSnapshotLocked().phaseRad;
}

bool isLocked() {
  return readSnapshotLocked().locked;
}

FrequencyPllSnapshot getFrequencySnapshot() {
  return readSnapshotLocked();
}

void resetFrequencyPll() {
  portENTER_CRITICAL(&g.lock);
  resetStateOnly();
  portEXIT_CRITICAL(&g.lock);
  if (g.queue) xQueueReset(g.queue);
}
