/*
  FakeFPGA ESP32 motor-control processor

  This sketch is the second ESP32 in the lab chain:

    FakeADS ESP32  -- SPI frames + DRDY -->  FakeFPGA ESP32  -- USB serial --> Raspberry Pi HMI

  It reads ADS131M08-like frames as an SPI master, calculates basic power-system
  measurements, and generates simple synchronous-motor control outputs. It does
  not try to be a real FPGA yet; it is a deterministic, configurable processor
  that gives the Raspberry a telemetry stream to monitor.
*/

#include <Arduino.h>
#include <SPI.h>
#include <strings.h>

#include "frequency_pll.h"

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

// ----------------------------- CONFIG ---------------------------------

static const int PIN_ADS_CS = 5;
static const int PIN_ADS_SCLK = 18;
static const int PIN_ADS_MOSI = 23;
static const int PIN_ADS_MISO = 19;
static const int PIN_ADS_DRDY = 4;

static const int PIN_MOTOR_RUN = 25;
static const int PIN_FIELD_PWM = 27;
static const int PIN_FIELD_ENABLE = 15;
static const int PIN_SYNC_PULSE = 26;
static const int PIN_FAULT = 33;
static const int PIN_HEARTBEAT_LED = 2; // onboard LED on common ESP32-WROOM DevKit boards

static const bool USE_EXTERNAL_PLANT_FEEDBACK = true;
static const int PIN_FB_BREAKER_CLOSED = 13;
static const int PIN_FB_SPEED_OK = 14;
static const int PIN_FB_FIELD_CURRENT = 16;
static const int PIN_FB_DISCHARGE_CURRENT = 17;
static const int PIN_FB_THERMAL_OK = 21;
static const int PIN_FB_EXCITER_READY = 22;
static const int PIN_FB_LOAD_READY = 32;
static const int PIN_FB_EMERGENCY_OK = 34;
static const int PIN_FB_PLANT_FAULT = 35;

static const uint8_t NUM_CHANNELS = 8;
static const uint8_t WORD_BYTES = 2;
static const uint16_t FRAME_BYTES = (1 + NUM_CHANNELS) * WORD_BYTES;
static const uint32_t DEFAULT_SPI_HZ = 10000000;
static const uint32_t NOMINAL_SAMPLE_RATE = 8000;
static const uint32_t TELEMETRY_MS = 100;
static const uint32_t SYNC_PULSE_US = 800;
static const uint32_t FIELD_PWM_HZ = 20000;
static const uint8_t FIELD_PWM_BITS = 10;
static const uint8_t FIELD_PWM_CH = 0;
static const float SCR_GATE_WIDTH_DEG = 8.0f;
static const float SCR_MAX_FIELD_DUTY = 0.95f;
static const UBaseType_t ADS_TASK_PRIORITY = configMAX_PRIORITIES - 1;
static const BaseType_t ADS_TASK_CORE = 0;
static const uint32_t ADS_TASK_STACK_WORDS = 4096;
static const float NOMINAL_SIGNAL_FREQ_HZ = 60.0f;
static const float VOLTAGE_RMS_UNITS = 120.0f;
static const float CURRENT_RMS_UNITS = 5.0f;
static const float FAKEADS_VOLTAGE_RMS_NORM = 0.5656854249f; // 0.8 / sqrt(2)
static const float FAKEADS_CURRENT_RMS_NORM = 0.3889087297f; // 0.55 / sqrt(2)
static const float VOLTAGE_SCALE = VOLTAGE_RMS_UNITS / FAKEADS_VOLTAGE_RMS_NORM;
static const float CURRENT_SCALE = CURRENT_RMS_UNITS / FAKEADS_CURRENT_RMS_NORM;
static const float PHASOR_STEP_RAD = 2.0f * PI * NOMINAL_SIGNAL_FREQ_HZ / (float)NOMINAL_SAMPLE_RATE;
static const uint32_t STARTING_MS = 2000;
static const uint32_t ACCELERATING_MS = 11000;
static const uint32_t FIELD_APPLY_MS = 1200;
static const uint32_t SYNC_VERIFY_MS = 1800;
static const uint32_t LOCKOUT_MS = 5000;
static const uint32_t DISCHARGE_PROVE_MS = 700;
static const uint32_t FULL_VOLTAGE_PROVE_MS = 2500;
static const uint32_t PULLOUT_AFTER_MS = 2200;
static const float PROT_VOLTAGE_MIN_RMS = 95.0f;
static const float PROT_VOLTAGE_MAX_RMS = 132.0f;
static const float PROT_PHASE_LOSS_RMS = 50.0f;
static const float PROT_SIGNAL_PRESENT_RMS = 20.0f;
static const float PROT_FREQ_MIN_HZ = 58.0f;
static const float PROT_FREQ_MAX_HZ = 62.0f;
static const float PROT_MAX_V_UNBALANCE_PCT = 8.0f;
static const float PROT_MIN_POWER_FACTOR = 0.65f;
static const uint8_t PROT_TRIP_PICKUP_COUNT = 2;
static const uint8_t PROT_TRIP_DROPOUT_COUNT = 4;

// ----------------------------- STATE ----------------------------------

struct Measurements {
  float freq_hz = 0.0f;
  float rocof_hz_s = 0.0f;
  float phase_deg = 0.0f;
  bool pll_locked = false;
  float va_rms = 0.0f;
  float vb_rms = 0.0f;
  float vc_rms = 0.0f;
  float ia_rms = 0.0f;
  float ib_rms = 0.0f;
  float ic_rms = 0.0f;
  float va_angle_deg = 0.0f;
  float vb_angle_deg = 0.0f;
  float vc_angle_deg = 0.0f;
  float ia_angle_deg = 0.0f;
  float ib_angle_deg = 0.0f;
  float ic_angle_deg = 0.0f;
  float p_total = 0.0f;
  float q_total = 0.0f;
  float pf = 0.0f;
  float v_unbalance = 0.0f;
  float field_duty = 0.0f;
};

struct Accumulators {
  double va2 = 0.0;
  double vb2 = 0.0;
  double vc2 = 0.0;
  double ia2 = 0.0;
  double ib2 = 0.0;
  double ic2 = 0.0;
  double p = 0.0;
  double phSin[6] = {};
  double phCos[6] = {};
  uint32_t count = 0;
};

enum ControlState : uint8_t {
  CTRL_IDLE = 0,
  CTRL_READY,
  CTRL_STARTING,
  CTRL_ACCELERATING,
  CTRL_FIELD_APPLY,
  CTRL_SYNC_VERIFY,
  CTRL_RUNNING,
  CTRL_FAULT,
  CTRL_LOCKOUT
};

enum FaultCode : uint8_t {
  FAULT_NONE = 0,
  FAULT_NO_DISCHARGE_CURRENT,
  FAULT_NO_FIELD_CURRENT,
  FAULT_LOW_POWER_FACTOR,
  FAULT_THERMAL,
  FAULT_INCOMPLETE_SEQUENCE,
  FAULT_PULLOUT,
  FAULT_UNDERVOLTAGE = 16,
  FAULT_OVERVOLTAGE,
  FAULT_UNDERFREQUENCY,
  FAULT_OVERFREQUENCY,
  FAULT_PHASE_LOSS,
  FAULT_VOLTAGE_UNBALANCE,
  FAULT_PHASE_SEQUENCE,
  FAULT_LOSS_OF_SIGNAL
};

enum ScenarioMode : uint8_t {
  SCENARIO_NORMAL = 0,
  SCENARIO_HEAVY_LOAD,
  SCENARIO_NO_DISCHARGE,
  SCENARIO_NO_FIELD,
  SCENARIO_LOW_PF,
  SCENARIO_THERMAL_TRIP,
  SCENARIO_PULLOUT,
  SCENARIO_FULLV_MISSING
};

struct ControlInputs {
  bool startCmd = false;
  bool stopCmd = false;
  bool fullVoltage = true;
  bool thermalOk = true;
  bool ack = false;
  bool reset = false;
};

struct PlantFeedback {
  bool breakerClosed = false;
  bool speedOk = false;
  bool fieldCurrent = false;
  bool dischargeCurrent = false;
  bool thermalOk = true;
  bool exciterReady = true;
  bool loadReady = true;
  bool emergencyOk = true;
  bool plantFault = false;
};

struct ControlOutputs {
  bool relay56k = false; // OK to start
  bool relayFs = false;  // field switch
  bool relayFax = false; // synchronized
  bool relayFal = true;  // fail-safe OK
  bool relayFwt = false;
  bool relayDst = false;
};

struct ScrGateCommand {
  float electricalAngleDeg = 0.0f;
  float firingAngleDeg = 135.0f;
  float gateWidthDeg = SCR_GATE_WIDTH_DEG;
  bool enabled = false;
  bool gate[6] = {};
  bool gateLatch[6] = {};
  uint32_t lastUpdateUs = 0;
};

struct ProcessSim {
  float speedPct = 0.0f;
  float slipHz = 60.0f;
  float dischargeAmps = 0.0f;
  float fieldVolts = 0.0f;
  float fieldAmps = 0.0f;
  float loadPct = 45.0f;
  float accelScale = 1.0f;
};

enum ProtectionBit : uint8_t {
  PROT_BIT_UNDERVOLTAGE = 0,
  PROT_BIT_OVERVOLTAGE,
  PROT_BIT_UNDERFREQUENCY,
  PROT_BIT_OVERFREQUENCY,
  PROT_BIT_PHASE_LOSS,
  PROT_BIT_VOLTAGE_UNBALANCE,
  PROT_BIT_LOW_POWER_FACTOR,
  PROT_BIT_PHASE_SEQUENCE,
  PROT_BIT_LOSS_OF_SIGNAL,
  PROT_BIT_COUNT
};

struct ElectricalProtection {
  uint16_t tripMask = 0;
  uint16_t permissiveMask = 0;
  uint8_t pickup[PROT_BIT_COUNT] = {};
  uint8_t dropout[PROT_BIT_COUNT] = {};
  bool voltageOk = false;
  bool frequencyOk = false;
  bool balanceOk = false;
  bool phaseSequenceOk = false;
  bool signalPresent = false;
  bool powerFactorOk = false;
  bool allPermissivesOk = false;
  bool phaseLoss = false;
};

static SPIClass adsSpi(VSPI);
static uint32_t adsSpiHz = DEFAULT_SPI_HZ;
static SPISettings adsSpiSettings(DEFAULT_SPI_HZ, MSBFIRST, SPI_MODE0);

static uint8_t rxFrame[FRAME_BYTES];
static uint8_t txDummyFrame[FRAME_BYTES];
static int32_t chRaw[NUM_CHANNELS];
static float chNorm[NUM_CHANNELS];
static Measurements meas;
static Accumulators accum;
static ControlInputs ctrlIn;
static PlantFeedback plantFb;
static ControlOutputs ctrlOut;
static ScrGateCommand scrCmd;
static ProcessSim processSim;
static ElectricalProtection eprot;
static ControlState ctrlState = CTRL_IDLE;
static ControlState lastCtrlState = CTRL_IDLE;
static FaultCode faultCode = FAULT_NONE;
static ScenarioMode scenarioMode = SCENARIO_NORMAL;
static uint32_t ctrlStateStartMs = 0;
static uint32_t ctrlLockoutUntilMs = 0;
static char ctrlStatusText[18] = "IDLE";
static portMUX_TYPE accumMux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t adsTaskHandle = nullptr;

static bool running = true;
static bool motorRun = false;
static bool autoField = true;
static bool fault = false;
static bool heartbeatLedState = false;
static uint32_t heartbeatLastToggleMs = 0;
static float voltageSetpoint = VOLTAGE_RMS_UNITS;
static float manualFieldDuty = 0.35f;
static float prevVa = 0.0f;
static float phasorTheta = 0.0f;
static uint32_t lastVaCrossUs = 0;
static volatile uint16_t drdyPending = 0;
static volatile uint32_t drdyEvents = 0;
static volatile uint32_t drdyDrops = 0;
static volatile uint32_t drdyLevelPrimes = 0;
static uint32_t lastTelemetryMs = 0;
static uint32_t lastStatsMs = 0;
static volatile uint32_t syncPulseReleaseUs = 0;
static uint32_t framesRead = 0;
static uint32_t badFrames = 0;
static uint32_t markerErrors = 0;
static uint32_t sequenceErrors = 0;
static uint16_t lastAdsSeq = 0;
static bool haveAdsSeq = false;
static uint32_t lastBadFramesControl = 0;
static uint32_t lastFramesRead = 0;
static float fps = 0.0f;

static char serialLine[96];
static uint8_t serialLineLen = 0;

static const char *controlStateName(ControlState state) {
  switch (state) {
    case CTRL_IDLE: return "IDLE";
    case CTRL_READY: return "READY";
    case CTRL_STARTING: return "STARTING";
    case CTRL_ACCELERATING: return "ACCEL";
    case CTRL_FIELD_APPLY: return "FIELD";
    case CTRL_SYNC_VERIFY: return "VERIFY";
    case CTRL_RUNNING: return "RUNNING";
    case CTRL_FAULT: return "FAULT";
    case CTRL_LOCKOUT: return "LOCKOUT";
  }
  return "UNKNOWN";
}

static const char *scenarioName(ScenarioMode scenario) {
  switch (scenario) {
    case SCENARIO_NORMAL: return "NORMAL";
    case SCENARIO_HEAVY_LOAD: return "HEAVY_LOAD";
    case SCENARIO_NO_DISCHARGE: return "NO_DISCHARGE";
    case SCENARIO_NO_FIELD: return "NO_FIELD";
    case SCENARIO_LOW_PF: return "LOW_PF";
    case SCENARIO_THERMAL_TRIP: return "THERMAL_TRIP";
    case SCENARIO_PULLOUT: return "PULLOUT";
    case SCENARIO_FULLV_MISSING: return "FULLV_MISSING";
  }
  return "NORMAL";
}

static bool parseScenario(const char *text, ScenarioMode &scenario) {
  if (!strcasecmp(text, "NORMAL")) scenario = SCENARIO_NORMAL;
  else if (!strcasecmp(text, "HEAVY_LOAD") || !strcasecmp(text, "HEAVY")) scenario = SCENARIO_HEAVY_LOAD;
  else if (!strcasecmp(text, "NO_DISCHARGE") || !strcasecmp(text, "NODISCH")) scenario = SCENARIO_NO_DISCHARGE;
  else if (!strcasecmp(text, "NO_FIELD") || !strcasecmp(text, "NOFIELD")) scenario = SCENARIO_NO_FIELD;
  else if (!strcasecmp(text, "LOW_PF") || !strcasecmp(text, "LOWPF")) scenario = SCENARIO_LOW_PF;
  else if (!strcasecmp(text, "THERMAL_TRIP") || !strcasecmp(text, "THERMAL")) scenario = SCENARIO_THERMAL_TRIP;
  else if (!strcasecmp(text, "PULLOUT")) scenario = SCENARIO_PULLOUT;
  else if (!strcasecmp(text, "FULLV_MISSING") || !strcasecmp(text, "NO_FULLV")) scenario = SCENARIO_FULLV_MISSING;
  else return false;
  return true;
}

static void applyScenarioDefaults() {
  ctrlIn.fullVoltage = scenarioMode != SCENARIO_FULLV_MISSING;
  ctrlIn.thermalOk = true;
  processSim.loadPct = scenarioMode == SCENARIO_HEAVY_LOAD ? 82.0f : 45.0f;
  processSim.accelScale = scenarioMode == SCENARIO_HEAVY_LOAD ? 0.58f : 1.0f;
}

static void setControlState(ControlState state) {
  if (ctrlState == state) return;
  lastCtrlState = ctrlState;
  ctrlState = state;
  ctrlStateStartMs = millis();
  strncpy(ctrlStatusText, controlStateName(state), sizeof(ctrlStatusText) - 1);
  ctrlStatusText[sizeof(ctrlStatusText) - 1] = '\0';
}

static void setControlFault(FaultCode code) {
  faultCode = code;
  ctrlIn.startCmd = false;
  ctrlOut.relayFs = false;
  ctrlOut.relayFax = false;
  ctrlOut.relayFwt = false;
  ctrlOut.relayDst = false;
  ctrlOut.relayFal = false;
  fault = true;
  setControlState(CTRL_FAULT);
}

static void IRAM_ATTR onAdsDrdyFalling() {
  BaseType_t higherPriorityTaskWoken = pdFALSE;
  if (drdyPending < 64) {
    drdyPending++;
  } else {
    drdyDrops++;
  }
  drdyEvents++;
  if (adsTaskHandle != nullptr) {
    vTaskNotifyGiveFromISR(adsTaskHandle, &higherPriorityTaskWoken);
  }
  if (higherPriorityTaskWoken == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}

// ----------------------------- HELPERS --------------------------------

static int32_t signExtend24(uint32_t v) {
  v &= 0x00FFFFFFu;
  if (v & 0x00800000u) v |= 0xFF000000u;
  return (int32_t)v;
}

static int32_t readS16(const uint8_t *p) {
  return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static float clampf(float value, float lo, float hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

static float wrapDegrees(float degrees) {
  while (degrees > 180.0f) degrees -= 360.0f;
  while (degrees <= -180.0f) degrees += 360.0f;
  return degrees;
}

static float wrap360(float degrees) {
  while (degrees >= 360.0f) degrees -= 360.0f;
  while (degrees < 0.0f) degrees += 360.0f;
  return degrees;
}

static float angularDistanceDeg(float a, float b) {
  float d = fabsf(wrap360(a) - wrap360(b));
  return d > 180.0f ? 360.0f - d : d;
}

static float smoothStep01(float x) {
  x = clampf(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

static float avgLineCurrent() {
  return (meas.ia_rms + meas.ib_rms + meas.ic_rms) / 3.0f;
}

static void writeFieldPwm(float duty) {
  duty = clampf(duty, 0.0f, 1.0f);
  const uint32_t raw = (uint32_t)(duty * ((1u << FIELD_PWM_BITS) - 1u));
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_FIELD_PWM, raw);
#else
  ledcWrite(FIELD_PWM_CH, raw);
#endif
}

static void configureFieldPwm() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_FIELD_PWM, FIELD_PWM_HZ, FIELD_PWM_BITS);
#else
  ledcSetup(FIELD_PWM_CH, FIELD_PWM_HZ, FIELD_PWM_BITS);
  ledcAttachPin(PIN_FIELD_PWM, FIELD_PWM_CH);
#endif
  writeFieldPwm(0.0f);
}

static void updateScrGateCommands() {
  const uint32_t nowUs = micros();
  if (scrCmd.lastUpdateUs == 0) {
    scrCmd.lastUpdateUs = nowUs;
    return;
  }

  uint32_t elapsedUs = nowUs - scrCmd.lastUpdateUs;
  scrCmd.lastUpdateUs = nowUs;
  if (elapsedUs > 5000u) elapsedUs = 5000u;
  scrCmd.electricalAngleDeg = wrap360(scrCmd.electricalAngleDeg +
                                      360.0f * NOMINAL_SIGNAL_FREQ_HZ * ((float)elapsedUs / 1000000.0f));

  scrCmd.enabled = motorRun && ctrlOut.relayFs && !fault;
  const float fieldRequest = scrCmd.enabled ? clampf(meas.field_duty / SCR_MAX_FIELD_DUTY, 0.0f, 1.0f) : 0.0f;
  scrCmd.firingAngleDeg = 135.0f - 105.0f * fieldRequest;

  static const float gateBaseDeg[6] = {0.0f, 60.0f, 120.0f, 180.0f, 240.0f, 300.0f};
  for (uint8_t i = 0; i < 6; i++) {
    const float gateAngle = wrap360(gateBaseDeg[i] + scrCmd.firingAngleDeg);
    scrCmd.gate[i] = scrCmd.enabled && angularDistanceDeg(scrCmd.electricalAngleDeg, gateAngle) <= (scrCmd.gateWidthDeg * 0.5f);
    if (scrCmd.gate[i]) scrCmd.gateLatch[i] = true;
  }
}

// ----------------------------- ADS INPUT -------------------------------

static bool readAdsFrame() {
  digitalWrite(PIN_ADS_CS, LOW);
  adsSpi.beginTransaction(adsSpiSettings);
  adsSpi.transferBytes(txDummyFrame, rxFrame, FRAME_BYTES);
  adsSpi.endTransaction();
  digitalWrite(PIN_ADS_CS, HIGH);

  const uint16_t status = ((uint16_t)rxFrame[0] << 8) | rxFrame[1];
  if ((status & 0xF000u) != 0x5000u) {
    badFrames++;
    markerErrors++;
    return false;
  }
  const uint16_t seq = status & 0x0FFFu;
  if (haveAdsSeq) {
    const uint16_t expected = (uint16_t)((lastAdsSeq + 1u) & 0x0FFFu);
    if (seq != expected) {
      sequenceErrors++;
    }
  }
  lastAdsSeq = seq;
  haveAdsSeq = true;

  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    chRaw[ch] = readS16(&rxFrame[(1 + ch) * WORD_BYTES]);
    chNorm[ch] = (float)chRaw[ch] / 32768.0f;
  }
  const int16_t vaForPll = (int16_t)chRaw[0];
  pushSamples(&vaForPll, 1);
  framesRead++;
  return true;
}

static void resetLinkStats() {
  noInterrupts();
  drdyPending = 0;
  drdyEvents = 0;
  drdyDrops = 0;
  drdyLevelPrimes = 0;
  interrupts();
  framesRead = 0;
  badFrames = 0;
  markerErrors = 0;
  sequenceErrors = 0;
  lastFramesRead = 0;
  lastBadFramesControl = 0;
  haveAdsSeq = false;
  fps = 0.0f;
}

static void sampleAdsIfReady() {
  if (!running) return;

  bool hasPending = false;
  noInterrupts();
  if (drdyPending > 0) {
    drdyPending--;
    hasPending = true;
  }
  interrupts();

  if (!hasPending) return;

  if (!readAdsFrame()) return;

  const float va = chNorm[0];
  const float vb = chNorm[1];
  const float vc = chNorm[2];
  const float ia = chNorm[4];
  const float ib = chNorm[5];
  const float ic = chNorm[6];
  const float phasorSamples[6] = {va, vb, vc, ia, ib, ic};
  const float phSin = sinf(phasorTheta);
  const float phCos = cosf(phasorTheta);
  phasorTheta += PHASOR_STEP_RAD;
  if (phasorTheta >= 2.0f * PI) phasorTheta -= 2.0f * PI;

  portENTER_CRITICAL(&accumMux);
  accum.va2 += (double)va * va;
  accum.vb2 += (double)vb * vb;
  accum.vc2 += (double)vc * vc;
  accum.ia2 += (double)ia * ia;
  accum.ib2 += (double)ib * ib;
  accum.ic2 += (double)ic * ic;
  accum.p += (double)va * ia + (double)vb * ib + (double)vc * ic;
  for (uint8_t i = 0; i < 6; i++) {
    accum.phSin[i] += (double)phasorSamples[i] * phSin;
    accum.phCos[i] += (double)phasorSamples[i] * phCos;
  }
  accum.count++;
  portEXIT_CRITICAL(&accumMux);

  if (prevVa < 0.0f && va >= 0.0f) {
    const uint32_t nowUs = micros();
    lastVaCrossUs = nowUs;
    scrCmd.electricalAngleDeg = 0.0f;
    scrCmd.lastUpdateUs = nowUs;
    digitalWrite(PIN_SYNC_PULSE, HIGH);
    syncPulseReleaseUs = nowUs + SYNC_PULSE_US;
  }
  prevVa = va;
}

static void adsAcquisitionTask(void *arg) {
  (void)arg;
  for (;;) {
    const uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
    if (notified == 0 && running && digitalRead(PIN_ADS_DRDY) == LOW) {
      noInterrupts();
      if (drdyPending == 0) {
        drdyPending++;
        drdyLevelPrimes++;
      }
      interrupts();
    }
    while (running) {
      uint16_t pendingSnapshot;
      noInterrupts();
      pendingSnapshot = drdyPending;
      interrupts();
      if (pendingSnapshot == 0) break;
      sampleAdsIfReady();
    }
  }
}

// -------------------------- MEASURE/CONTROL ----------------------------

static void publishWindowMeasurements() {
  FrequencyPllSnapshot pll = getFrequencySnapshot();
  meas.freq_hz = pll.frequencyHz;
  meas.rocof_hz_s = pll.rocofHzPerSec;
  meas.phase_deg = pll.phaseRad * 57.2957795131f;
  meas.pll_locked = pll.locked;

  Accumulators window;
  portENTER_CRITICAL(&accumMux);
  window = accum;
  accum = Accumulators{};
  portEXIT_CRITICAL(&accumMux);

  if (window.count == 0) return;
  const double inv = 1.0 / (double)window.count;
  meas.va_rms = sqrt(window.va2 * inv) * VOLTAGE_SCALE;
  meas.vb_rms = sqrt(window.vb2 * inv) * VOLTAGE_SCALE;
  meas.vc_rms = sqrt(window.vc2 * inv) * VOLTAGE_SCALE;
  meas.ia_rms = sqrt(window.ia2 * inv) * CURRENT_SCALE;
  meas.ib_rms = sqrt(window.ib2 * inv) * CURRENT_SCALE;
  meas.ic_rms = sqrt(window.ic2 * inv) * CURRENT_SCALE;
  meas.p_total = window.p * inv * VOLTAGE_SCALE * CURRENT_SCALE;

  float angleDeg[6];
  for (uint8_t i = 0; i < 6; i++) {
    angleDeg[i] = atan2f((float)window.phCos[i], (float)window.phSin[i]) * 57.2957795131f;
  }
  const float vaRef = angleDeg[0];
  meas.va_angle_deg = 0.0f;
  meas.vb_angle_deg = wrapDegrees(angleDeg[1] - vaRef);
  meas.vc_angle_deg = wrapDegrees(angleDeg[2] - vaRef);
  meas.ia_angle_deg = wrapDegrees(angleDeg[3] - vaRef);
  meas.ib_angle_deg = wrapDegrees(angleDeg[4] - vaRef);
  meas.ic_angle_deg = wrapDegrees(angleDeg[5] - vaRef);

  const float s = 3.0f * ((meas.va_rms + meas.vb_rms + meas.vc_rms) / 3.0f) *
                  ((meas.ia_rms + meas.ib_rms + meas.ic_rms) / 3.0f);
  meas.pf = s > 0.0001f ? clampf(meas.p_total / s, -1.0f, 1.0f) : 0.0f;
  meas.q_total = sqrtf(max(0.0f, s * s - meas.p_total * meas.p_total));

  const float v_avg = (meas.va_rms + meas.vb_rms + meas.vc_rms) / 3.0f;
  const float v_max_dev = max(max(fabsf(meas.va_rms - v_avg), fabsf(meas.vb_rms - v_avg)),
                              fabsf(meas.vc_rms - v_avg));
  meas.v_unbalance = v_avg > 0.001f ? 100.0f * v_max_dev / v_avg : 0.0f;

}

static void updateMotorControl() {
  if (autoField) {
    const float error = voltageSetpoint - meas.va_rms;
    meas.field_duty = clampf(meas.field_duty + error * 0.025f, 0.0f, 0.95f);
  } else {
    meas.field_duty = clampf(manualFieldDuty, 0.0f, 0.95f);
  }

  const uint32_t recentBadFrames = badFrames - lastBadFramesControl;
  lastBadFramesControl = badFrames;
  const bool measurementFault = activeElectricalFault() != FAULT_NONE || recentBadFrames > 10;
  fault = measurementFault || ctrlState == CTRL_FAULT || ctrlState == CTRL_LOCKOUT;
  const bool fieldEnable = motorRun && ctrlOut.relayFs && !fault;
  digitalWrite(PIN_FAULT, fault ? HIGH : LOW);
  digitalWrite(PIN_MOTOR_RUN, (motorRun && !fault) ? HIGH : LOW);
  digitalWrite(PIN_FIELD_ENABLE, fieldEnable ? HIGH : LOW);
  writeFieldPwm(fieldEnable ? meas.field_duty : 0.0f);
}

static void resetProcessSim() {
  processSim.speedPct = 0.0f;
  processSim.slipHz = 60.0f;
  processSim.dischargeAmps = 0.0f;
  processSim.fieldVolts = 0.0f;
  processSim.fieldAmps = 0.0f;
  processSim.loadPct = scenarioMode == SCENARIO_HEAVY_LOAD ? 82.0f : 45.0f;
  processSim.accelScale = scenarioMode == SCENARIO_HEAVY_LOAD ? 0.58f : 1.0f;
}

static void readPlantFeedback() {
  if (!USE_EXTERNAL_PLANT_FEEDBACK) {
    plantFb = PlantFeedback{};
    return;
  }

  plantFb.breakerClosed = digitalRead(PIN_FB_BREAKER_CLOSED) == HIGH;
  plantFb.speedOk = digitalRead(PIN_FB_SPEED_OK) == HIGH;
  plantFb.fieldCurrent = digitalRead(PIN_FB_FIELD_CURRENT) == HIGH;
  plantFb.dischargeCurrent = digitalRead(PIN_FB_DISCHARGE_CURRENT) == HIGH;
  plantFb.thermalOk = digitalRead(PIN_FB_THERMAL_OK) == HIGH;
  plantFb.exciterReady = digitalRead(PIN_FB_EXCITER_READY) == HIGH;
  plantFb.loadReady = digitalRead(PIN_FB_LOAD_READY) == HIGH;
  plantFb.emergencyOk = digitalRead(PIN_FB_EMERGENCY_OK) == HIGH;
  plantFb.plantFault = digitalRead(PIN_FB_PLANT_FAULT) == HIGH;

  ctrlIn.fullVoltage = plantFb.breakerClosed;
  ctrlIn.thermalOk = plantFb.thermalOk && plantFb.emergencyOk && !plantFb.plantFault;
}

static void updateHeartbeatLed(uint32_t now) {
  const uint32_t periodMs = fault ? 150 : (ctrlState == CTRL_RUNNING ? 500 : 900);
  if (now - heartbeatLastToggleMs < periodMs) return;
  heartbeatLastToggleMs = now;
  heartbeatLedState = !heartbeatLedState;
  digitalWrite(PIN_HEARTBEAT_LED, heartbeatLedState ? HIGH : LOW);
}

static float simulatedDischargeAmps(float progress) {
  if (scenarioMode == SCENARIO_NO_DISCHARGE) return 0.0f;
  return (1.4f + 3.8f * progress) * (1.0f + processSim.loadPct * 0.003f);
}

static float simulatedFieldAmps(float progress) {
  if (scenarioMode == SCENARIO_NO_FIELD) return 0.0f;
  return 4.0f * progress;
}

static float effectivePowerFactor() {
  if (scenarioMode == SCENARIO_LOW_PF) return 0.42f;
  return fabsf(meas.pf);
}

static bool fieldCurrentProven() {
  return plantFb.fieldCurrent || processSim.fieldAmps >= 0.5f;
}

static FaultCode protectionFaultForBit(uint8_t bit) {
  switch (bit) {
    case PROT_BIT_UNDERVOLTAGE: return FAULT_UNDERVOLTAGE;
    case PROT_BIT_OVERVOLTAGE: return FAULT_OVERVOLTAGE;
    case PROT_BIT_UNDERFREQUENCY: return FAULT_UNDERFREQUENCY;
    case PROT_BIT_OVERFREQUENCY: return FAULT_OVERFREQUENCY;
    case PROT_BIT_PHASE_LOSS: return FAULT_PHASE_LOSS;
    case PROT_BIT_VOLTAGE_UNBALANCE: return FAULT_VOLTAGE_UNBALANCE;
    case PROT_BIT_LOW_POWER_FACTOR: return FAULT_LOW_POWER_FACTOR;
    case PROT_BIT_PHASE_SEQUENCE: return FAULT_PHASE_SEQUENCE;
    case PROT_BIT_LOSS_OF_SIGNAL: return FAULT_LOSS_OF_SIGNAL;
  }
  return FAULT_NONE;
}

static void updateProtectionBit(uint8_t bit, bool rawActive) {
  const uint16_t mask = (uint16_t)(1u << bit);
  if (rawActive) {
    if (eprot.pickup[bit] < PROT_TRIP_PICKUP_COUNT) eprot.pickup[bit]++;
    eprot.dropout[bit] = 0;
    if (eprot.pickup[bit] >= PROT_TRIP_PICKUP_COUNT) eprot.tripMask |= mask;
  } else {
    if (eprot.dropout[bit] < PROT_TRIP_DROPOUT_COUNT) eprot.dropout[bit]++;
    eprot.pickup[bit] = 0;
    if (eprot.dropout[bit] >= PROT_TRIP_DROPOUT_COUNT) eprot.tripMask &= (uint16_t)~mask;
  }
}

static bool isPhaseSequenceOk() {
  const float vbErr = fabsf(wrapDegrees(meas.vb_angle_deg + 120.0f));
  const float vcErr = fabsf(wrapDegrees(meas.vc_angle_deg - 120.0f));
  return vbErr < 35.0f && vcErr < 35.0f;
}

static void evaluateElectricalProtection() {
  const float vAvg = (meas.va_rms + meas.vb_rms + meas.vc_rms) / 3.0f;
  const bool haveSamples = framesRead > 200;
  const bool signalPresent = haveSamples && vAvg >= PROT_SIGNAL_PRESENT_RMS;
  const bool phaseLoss = signalPresent &&
                         (meas.va_rms < PROT_PHASE_LOSS_RMS ||
                          meas.vb_rms < PROT_PHASE_LOSS_RMS ||
                          meas.vc_rms < PROT_PHASE_LOSS_RMS);
  const bool underVoltage = signalPresent && vAvg < PROT_VOLTAGE_MIN_RMS;
  const bool overVoltage = signalPresent && vAvg > PROT_VOLTAGE_MAX_RMS;
  const bool underFreq = signalPresent && meas.pll_locked && meas.freq_hz < PROT_FREQ_MIN_HZ;
  const bool overFreq = signalPresent && meas.pll_locked && meas.freq_hz > PROT_FREQ_MAX_HZ;
  const bool vUnbalance = signalPresent && meas.v_unbalance > PROT_MAX_V_UNBALANCE_PCT;
  const bool lowPf = signalPresent && (meas.ia_rms + meas.ib_rms + meas.ic_rms) > 0.6f &&
                     fabsf(meas.pf) < PROT_MIN_POWER_FACTOR;
  const bool phaseSequenceBad = signalPresent && !phaseLoss && !isPhaseSequenceOk();
  const bool lossOfSignal = haveSamples && !signalPresent;

  updateProtectionBit(PROT_BIT_UNDERVOLTAGE, underVoltage);
  updateProtectionBit(PROT_BIT_OVERVOLTAGE, overVoltage);
  updateProtectionBit(PROT_BIT_UNDERFREQUENCY, underFreq);
  updateProtectionBit(PROT_BIT_OVERFREQUENCY, overFreq);
  updateProtectionBit(PROT_BIT_PHASE_LOSS, phaseLoss);
  updateProtectionBit(PROT_BIT_VOLTAGE_UNBALANCE, vUnbalance);
  updateProtectionBit(PROT_BIT_LOW_POWER_FACTOR, lowPf);
  updateProtectionBit(PROT_BIT_PHASE_SEQUENCE, phaseSequenceBad);
  updateProtectionBit(PROT_BIT_LOSS_OF_SIGNAL, lossOfSignal);

  eprot.signalPresent = signalPresent;
  eprot.phaseLoss = phaseLoss;
  eprot.voltageOk = signalPresent && !underVoltage && !overVoltage && !phaseLoss;
  eprot.frequencyOk = signalPresent && meas.pll_locked && !underFreq && !overFreq;
  eprot.balanceOk = signalPresent && !vUnbalance;
  eprot.phaseSequenceOk = signalPresent && !phaseSequenceBad;
  eprot.powerFactorOk = signalPresent && !lowPf;
  eprot.allPermissivesOk = eprot.voltageOk && eprot.frequencyOk && eprot.balanceOk &&
                           eprot.phaseSequenceOk && eprot.powerFactorOk;

  eprot.permissiveMask = 0;
  if (eprot.voltageOk) eprot.permissiveMask |= 1u << 0;
  if (eprot.frequencyOk) eprot.permissiveMask |= 1u << 1;
  if (eprot.balanceOk) eprot.permissiveMask |= 1u << 2;
  if (eprot.phaseSequenceOk) eprot.permissiveMask |= 1u << 3;
  if (eprot.signalPresent) eprot.permissiveMask |= 1u << 4;
  if (eprot.powerFactorOk) eprot.permissiveMask |= 1u << 5;
}

static FaultCode activeElectricalFault() {
  for (uint8_t bit = 0; bit < PROT_BIT_COUNT; bit++) {
    if (eprot.tripMask & (1u << bit)) {
      if (bit == PROT_BIT_LOW_POWER_FACTOR &&
          (ctrlState == CTRL_STARTING || ctrlState == CTRL_ACCELERATING || ctrlState == CTRL_FIELD_APPLY)) {
        return FAULT_NONE;
      }
      if (bit == PROT_BIT_LOW_POWER_FACTOR && ctrlState != CTRL_IDLE && ctrlState != CTRL_READY &&
          (avgLineCurrent() > 5.4f || (USE_EXTERNAL_PLANT_FEEDBACK && !plantFb.loadReady))) {
        return FAULT_PULLOUT;
      }
      return protectionFaultForBit(bit);
    }
  }
  return FAULT_NONE;
}

static void updateSequenceControl() {
  const uint32_t now = millis();
  const uint32_t elapsed = now - ctrlStateStartMs;
  evaluateElectricalProtection();
  readPlantFeedback();

  const FaultCode electricalFault = activeElectricalFault();
  if (electricalFault != FAULT_NONE &&
      ctrlState != CTRL_IDLE && ctrlState != CTRL_READY &&
      ctrlState != CTRL_FAULT && ctrlState != CTRL_LOCKOUT) {
    setControlFault(electricalFault);
  }

  if (scenarioMode == SCENARIO_THERMAL_TRIP && (ctrlState == CTRL_STARTING || ctrlState == CTRL_ACCELERATING) && elapsed > 900) {
    ctrlIn.thermalOk = false;
  }

  if (!ctrlIn.thermalOk && ctrlState != CTRL_FAULT) {
    setControlFault(FAULT_THERMAL);
  }

  if (ctrlIn.stopCmd) {
    ctrlIn.stopCmd = false;
    ctrlIn.startCmd = false;
    ctrlOut = ControlOutputs{};
    ctrlOut.relayFal = true;
    motorRun = false;
    resetProcessSim();
    faultCode = FAULT_NONE;
    fault = false;
    setControlState(CTRL_IDLE);
  }

  switch (ctrlState) {
    case CTRL_IDLE:
      ctrlOut = ControlOutputs{};
      ctrlOut.relayFal = true;
      ctrlOut.relay56k = false;
      motorRun = false;
      resetProcessSim();
      if (ctrlIn.thermalOk && (int32_t)(now - ctrlLockoutUntilMs) >= 0) {
        setControlState(CTRL_READY);
      }
      break;

    case CTRL_READY:
      motorRun = false;
      ctrlOut.relay56k = eprot.allPermissivesOk;
      ctrlOut.relayFal = true;
      processSim.speedPct = 0.0f;
      processSim.slipHz = 60.0f;
      if (ctrlIn.startCmd && eprot.allPermissivesOk) {
        applyScenarioDefaults();
        motorRun = true;
        ctrlIn.startCmd = false;
        ctrlOut.relay56k = false;
        setControlState(CTRL_STARTING);
      } else if (ctrlIn.startCmd && !eprot.allPermissivesOk) {
        ctrlIn.startCmd = false;
      }
      break;

    case CTRL_STARTING:
      processSim.speedPct = 4.0f + 18.0f * smoothStep01(((float)elapsed / (float)STARTING_MS) * processSim.accelScale);
      processSim.slipHz = max(47.0f, 60.0f * (1.0f - processSim.speedPct / 100.0f));
      processSim.dischargeAmps = max(simulatedDischargeAmps(0.0f), avgLineCurrent() * 0.85f);
      ctrlOut.relayDst = true;
      if (elapsed >= DISCHARGE_PROVE_MS &&
          (USE_EXTERNAL_PLANT_FEEDBACK ? !plantFb.dischargeCurrent : processSim.dischargeAmps < 0.2f)) {
        setControlFault(FAULT_NO_DISCHARGE_CURRENT);
      } else if (elapsed >= FULL_VOLTAGE_PROVE_MS && !ctrlIn.fullVoltage) {
        setControlFault(FAULT_INCOMPLETE_SEQUENCE);
      } else if (elapsed >= STARTING_MS) {
        setControlState(CTRL_ACCELERATING);
      }
      break;

    case CTRL_ACCELERATING: {
      const float progress = smoothStep01(((float)elapsed / (float)ACCELERATING_MS) * processSim.accelScale);
      const float pfAssist = clampf((fabsf(meas.pf) - 0.50f) / 0.40f, 0.0f, 1.0f);
      processSim.speedPct = 22.0f + progress * (70.0f + 4.0f * pfAssist);
      processSim.slipHz = max(1.8f, 60.0f * (1.0f - processSim.speedPct / 100.0f));
      processSim.dischargeAmps = max(simulatedDischargeAmps(progress), avgLineCurrent() * (1.0f - 0.25f * progress));
      ctrlOut.relayDst = true;
      ctrlOut.relayFwt = progress > 0.5f;
      if (USE_EXTERNAL_PLANT_FEEDBACK && processSim.speedPct < 85.0f && !plantFb.dischargeCurrent) {
        setControlFault(FAULT_NO_DISCHARGE_CURRENT);
      } else if (!ctrlIn.fullVoltage) {
        setControlFault(FAULT_INCOMPLETE_SEQUENCE);
      } else if ((USE_EXTERNAL_PLANT_FEEDBACK && plantFb.speedOk) || processSim.slipHz <= 3.5f || elapsed >= ACCELERATING_MS) {
        setControlState(CTRL_FIELD_APPLY);
      }
      break;
    }

    case CTRL_FIELD_APPLY: {
      ctrlOut.relayFs = true;
      ctrlOut.relayFwt = true;
      ctrlOut.relayDst = false;
      processSim.speedPct = min(99.6f, processSim.speedPct + 0.35f + 0.25f * clampf(fabsf(meas.pf), 0.0f, 1.0f));
      processSim.slipHz = max(0.25f, 60.0f * (1.0f - processSim.speedPct / 100.0f));
      const float fieldProgress = clampf((float)elapsed / (float)FIELD_APPLY_MS, 0.0f, 1.0f);
      processSim.fieldAmps = simulatedFieldAmps(fieldProgress);
      processSim.fieldVolts = processSim.fieldAmps > 0.1f ? 125.0f : 0.0f;
      if (USE_EXTERNAL_PLANT_FEEDBACK && elapsed > 250 && !fieldCurrentProven()) {
          setControlFault(FAULT_NO_FIELD_CURRENT);
        } else if (elapsed >= FIELD_APPLY_MS) {
          if (!USE_EXTERNAL_PLANT_FEEDBACK && processSim.fieldAmps < 0.5f) setControlFault(FAULT_NO_FIELD_CURRENT);
          else setControlState(CTRL_SYNC_VERIFY);
        }
        break;
      }

    case CTRL_SYNC_VERIFY:
      ctrlOut.relayFs = true;
      processSim.speedPct = 100.0f;
      processSim.slipHz = 0.0f;
      processSim.fieldVolts = 125.0f;
      processSim.fieldAmps = simulatedFieldAmps(1.0f);
      if (USE_EXTERNAL_PLANT_FEEDBACK && !fieldCurrentProven()) {
        setControlFault(FAULT_NO_FIELD_CURRENT);
        break;
      }
      if (elapsed >= SYNC_VERIFY_MS) {
        if (!USE_EXTERNAL_PLANT_FEEDBACK && processSim.fieldAmps < 0.5f) setControlFault(FAULT_NO_FIELD_CURRENT);
        else if (effectivePowerFactor() < 0.65f) setControlFault(FAULT_LOW_POWER_FACTOR);
        else {
          ctrlOut.relayFax = true;
          setControlState(CTRL_RUNNING);
        }
      }
      break;

    case CTRL_RUNNING:
      ctrlOut.relayFs = true;
      ctrlOut.relayFax = true;
      ctrlOut.relayFal = true;
      processSim.speedPct = effectivePowerFactor() < 0.60f ? 96.0f : 100.0f;
      processSim.slipHz = effectivePowerFactor() < 0.60f ? 2.4f : 0.0f;
      processSim.fieldVolts = 125.0f;
      processSim.fieldAmps = simulatedFieldAmps(1.0f);
      if (USE_EXTERNAL_PLANT_FEEDBACK && !fieldCurrentProven()) {
        setControlFault(FAULT_NO_FIELD_CURRENT);
      } else if (USE_EXTERNAL_PLANT_FEEDBACK && !plantFb.loadReady) {
        setControlFault(FAULT_PULLOUT);
      } else if (effectivePowerFactor() < 0.55f) {
        setControlFault(FAULT_LOW_POWER_FACTOR);
      }
      break;

    case CTRL_FAULT:
      ctrlOut.relayFal = false;
      ctrlOut.relay56k = false;
      ctrlOut.relayFs = false;
      ctrlOut.relayFax = false;
      ctrlOut.relayFwt = false;
      ctrlOut.relayDst = false;
      motorRun = false;
      processSim.fieldVolts = 0.0f;
      processSim.fieldAmps = 0.0f;
      if (ctrlIn.ack) {
        ctrlIn.ack = false;
        ctrlLockoutUntilMs = now + LOCKOUT_MS;
        setControlState(CTRL_LOCKOUT);
      }
      break;

    case CTRL_LOCKOUT:
      ctrlOut.relayFal = true;
      ctrlOut.relay56k = false;
      resetProcessSim();
      if (ctrlIn.reset && (int32_t)(now - ctrlLockoutUntilMs) >= 0) {
        ctrlIn.reset = false;
        faultCode = FAULT_NONE;
        fault = false;
        setControlState(CTRL_IDLE);
      }
      break;
  }
}

static void updateStats() {
  const uint32_t now = millis();
  if (now - lastStatsMs < 1000) return;
  const uint32_t elapsed = now - lastStatsMs;
  fps = ((float)(framesRead - lastFramesRead) * 1000.0f) / (float)elapsed;
  lastFramesRead = framesRead;
  lastStatsMs = now;
}

static void printTelemetry() {
  Serial.print(F("FPGA seq=")); Serial.print(framesRead);
  Serial.print(F(" fps=")); Serial.print(fps, 1);
  Serial.print(F(" bad=")); Serial.print(badFrames);
  Serial.print(F(" mark=")); Serial.print(markerErrors);
  Serial.print(F(" seqerr=")); Serial.print(sequenceErrors);
  Serial.print(F(" drdy=")); Serial.print((uint32_t)drdyEvents);
  Serial.print(F(" drops=")); Serial.print((uint32_t)drdyDrops);
  Serial.print(F(" prime=")); Serial.print((uint32_t)drdyLevelPrimes);
  Serial.print(F(" pend=")); Serial.print((uint32_t)drdyPending);
  Serial.print(F(" f=")); Serial.print(meas.freq_hz, 3);
  Serial.print(F(" rocof=")); Serial.print(meas.rocof_hz_s, 3);
  Serial.print(F(" phase=")); Serial.print(meas.phase_deg, 2);
  Serial.print(F(" lock=")); Serial.print(meas.pll_locked ? 1 : 0);
  Serial.print(F(" va=")); Serial.print(meas.va_rms, 4);
  Serial.print(F(" vb=")); Serial.print(meas.vb_rms, 4);
  Serial.print(F(" vc=")); Serial.print(meas.vc_rms, 4);
  Serial.print(F(" ia=")); Serial.print(meas.ia_rms, 4);
  Serial.print(F(" ib=")); Serial.print(meas.ib_rms, 4);
  Serial.print(F(" ic=")); Serial.print(meas.ic_rms, 4);
  Serial.print(F(" ava=")); Serial.print(meas.va_angle_deg, 2);
  Serial.print(F(" avb=")); Serial.print(meas.vb_angle_deg, 2);
  Serial.print(F(" avc=")); Serial.print(meas.vc_angle_deg, 2);
  Serial.print(F(" aia=")); Serial.print(meas.ia_angle_deg, 2);
  Serial.print(F(" aib=")); Serial.print(meas.ib_angle_deg, 2);
  Serial.print(F(" aic=")); Serial.print(meas.ic_angle_deg, 2);
  Serial.print(F(" p=")); Serial.print(meas.p_total, 4);
  Serial.print(F(" q=")); Serial.print(meas.q_total, 4);
  Serial.print(F(" pf=")); Serial.print(meas.pf, 4);
  Serial.print(F(" vunb=")); Serial.print(meas.v_unbalance, 2);
  Serial.print(F(" field=")); Serial.print(meas.field_duty, 3);
  Serial.print(F(" run=")); Serial.print(motorRun ? 1 : 0);
  Serial.print(F(" auto=")); Serial.print(autoField ? 1 : 0);
  Serial.print(F(" fault=")); Serial.print(fault ? 1 : 0);
  Serial.print(F(" ctrl=")); Serial.print((uint8_t)ctrlState);
  Serial.print(F(" lastctrl=")); Serial.print((uint8_t)lastCtrlState);
  Serial.print(F(" tripmask=")); Serial.print(eprot.tripMask);
  Serial.print(F(" permask=")); Serial.print(eprot.permissiveMask);
  Serial.print(F(" p_vok=")); Serial.print(eprot.voltageOk ? 1 : 0);
  Serial.print(F(" p_fok=")); Serial.print(eprot.frequencyOk ? 1 : 0);
  Serial.print(F(" p_bal=")); Serial.print(eprot.balanceOk ? 1 : 0);
  Serial.print(F(" p_seq=")); Serial.print(eprot.phaseSequenceOk ? 1 : 0);
  Serial.print(F(" p_sig=")); Serial.print(eprot.signalPresent ? 1 : 0);
  Serial.print(F(" p_pf=")); Serial.print(eprot.powerFactorOk ? 1 : 0);
  Serial.print(F(" t_uv=")); Serial.print((eprot.tripMask & (1u << PROT_BIT_UNDERVOLTAGE)) ? 1 : 0);
  Serial.print(F(" t_ov=")); Serial.print((eprot.tripMask & (1u << PROT_BIT_OVERVOLTAGE)) ? 1 : 0);
  Serial.print(F(" t_uf=")); Serial.print((eprot.tripMask & (1u << PROT_BIT_UNDERFREQUENCY)) ? 1 : 0);
  Serial.print(F(" t_of=")); Serial.print((eprot.tripMask & (1u << PROT_BIT_OVERFREQUENCY)) ? 1 : 0);
  Serial.print(F(" t_ploss=")); Serial.print((eprot.tripMask & (1u << PROT_BIT_PHASE_LOSS)) ? 1 : 0);
  Serial.print(F(" t_vunb=")); Serial.print((eprot.tripMask & (1u << PROT_BIT_VOLTAGE_UNBALANCE)) ? 1 : 0);
  Serial.print(F(" t_lpf=")); Serial.print((eprot.tripMask & (1u << PROT_BIT_LOW_POWER_FACTOR)) ? 1 : 0);
  Serial.print(F(" t_pseq=")); Serial.print((eprot.tripMask & (1u << PROT_BIT_PHASE_SEQUENCE)) ? 1 : 0);
  Serial.print(F(" t_los=")); Serial.print((eprot.tripMask & (1u << PROT_BIT_LOSS_OF_SIGNAL)) ? 1 : 0);
  Serial.print(F(" scenario=")); Serial.print((uint8_t)scenarioMode);
  Serial.print(F(" faultcode=")); Serial.print((uint8_t)faultCode);
  Serial.print(F(" startin=")); Serial.print(ctrlIn.startCmd ? 1 : 0);
  Serial.print(F(" fullv=")); Serial.print(ctrlIn.fullVoltage ? 1 : 0);
  Serial.print(F(" thermok=")); Serial.print(ctrlIn.thermalOk ? 1 : 0);
  Serial.print(F(" pfb_breaker=")); Serial.print(plantFb.breakerClosed ? 1 : 0);
  Serial.print(F(" pfb_speed=")); Serial.print(plantFb.speedOk ? 1 : 0);
  Serial.print(F(" pfb_field=")); Serial.print(plantFb.fieldCurrent ? 1 : 0);
  Serial.print(F(" pfb_discharge=")); Serial.print(plantFb.dischargeCurrent ? 1 : 0);
  Serial.print(F(" pfb_thermal=")); Serial.print(plantFb.thermalOk ? 1 : 0);
  Serial.print(F(" pfb_exciter=")); Serial.print(plantFb.exciterReady ? 1 : 0);
  Serial.print(F(" pfb_load=")); Serial.print(plantFb.loadReady ? 1 : 0);
  Serial.print(F(" pfb_emergency=")); Serial.print(plantFb.emergencyOk ? 1 : 0);
  Serial.print(F(" pfb_fault=")); Serial.print(plantFb.plantFault ? 1 : 0);
  Serial.print(F(" r56k=")); Serial.print(ctrlOut.relay56k ? 1 : 0);
  Serial.print(F(" fs=")); Serial.print(ctrlOut.relayFs ? 1 : 0);
  Serial.print(F(" fax=")); Serial.print(ctrlOut.relayFax ? 1 : 0);
  Serial.print(F(" fal=")); Serial.print(ctrlOut.relayFal ? 1 : 0);
  Serial.print(F(" fwt=")); Serial.print(ctrlOut.relayFwt ? 1 : 0);
  Serial.print(F(" dst=")); Serial.print(ctrlOut.relayDst ? 1 : 0);
  Serial.print(F(" scrcmd_en=")); Serial.print(scrCmd.enabled ? 1 : 0);
  Serial.print(F(" scrcmd_fire=")); Serial.print(scrCmd.firingAngleDeg, 2);
  Serial.print(F(" scrcmd_angle=")); Serial.print(scrCmd.electricalAngleDeg, 2);
  Serial.print(F(" scrcmd_g1=")); Serial.print(scrCmd.gateLatch[0] ? 1 : 0);
  Serial.print(F(" scrcmd_g2=")); Serial.print(scrCmd.gateLatch[1] ? 1 : 0);
  Serial.print(F(" scrcmd_g3=")); Serial.print(scrCmd.gateLatch[2] ? 1 : 0);
  Serial.print(F(" scrcmd_g4=")); Serial.print(scrCmd.gateLatch[3] ? 1 : 0);
  Serial.print(F(" scrcmd_g5=")); Serial.print(scrCmd.gateLatch[4] ? 1 : 0);
  Serial.print(F(" scrcmd_g6=")); Serial.print(scrCmd.gateLatch[5] ? 1 : 0);
  Serial.print(F(" speed=")); Serial.print(processSim.speedPct, 1);
  Serial.print(F(" slip=")); Serial.print(processSim.slipHz, 2);
  Serial.print(F(" load=")); Serial.print(processSim.loadPct, 1);
  Serial.print(F(" accels=")); Serial.print(processSim.accelScale, 2);
  Serial.print(F(" disca=")); Serial.print(processSim.dischargeAmps, 2);
  Serial.print(F(" fieldv=")); Serial.print(processSim.fieldVolts, 1);
  Serial.print(F(" fielda=")); Serial.print(processSim.fieldAmps, 2);
  Serial.print(F(" ctimer=")); Serial.println(millis() - ctrlStateStartMs);
  for (uint8_t i = 0; i < 6; i++) scrCmd.gateLatch[i] = false;
}

// ----------------------------- SERIAL ---------------------------------

static void printConfig() {
  Serial.println(F("FakeFPGA ESP32"));
  Serial.print(F("ADS_SPI_HZ=")); Serial.println(adsSpiHz);
  Serial.print(F("NOMINAL_SAMPLE_RATE=")); Serial.println(NOMINAL_SAMPLE_RATE);
  Serial.print(F("ADS_TASK_PRIORITY=")); Serial.println(ADS_TASK_PRIORITY);
  Serial.print(F("ADS_TASK_CORE=")); Serial.println(ADS_TASK_CORE);
  Serial.print(F("SCENARIO=")); Serial.println(scenarioName(scenarioMode));
  Serial.print(F("FRAME_BYTES=")); Serial.println(FRAME_BYTES);
  Serial.print(F("DRDY_PIN=")); Serial.println(PIN_ADS_DRDY);
  Serial.print(F("MOTOR_RUN_PIN=")); Serial.println(PIN_MOTOR_RUN);
  Serial.print(F("FIELD_ENABLE_PIN=")); Serial.println(PIN_FIELD_ENABLE);
  Serial.print(F("FIELD_PWM_PIN=")); Serial.println(PIN_FIELD_PWM);
  Serial.print(F("SYNC_PULSE_PIN=")); Serial.println(PIN_SYNC_PULSE);
  Serial.print(F("FAULT_PIN=")); Serial.println(PIN_FAULT);
  Serial.print(F("HEARTBEAT_LED_PIN=")); Serial.println(PIN_HEARTBEAT_LED);
  Serial.print(F("EXTERNAL_PLANT_FEEDBACK=")); Serial.println(USE_EXTERNAL_PLANT_FEEDBACK ? F("ON") : F("OFF"));
}

static void handleCommand(char *line) {
  char *cmd = strtok(line, " \t\r\n");
  if (!cmd) return;

  if (!strcasecmp(cmd, "START")) {
    running = true;
    Serial.println(F("OK"));
  } else if (!strcasecmp(cmd, "STOP")) {
    running = false;
    Serial.println(F("OK"));
  } else if (!strcasecmp(cmd, "STARTSEQ")) {
    ctrlIn.startCmd = true;
    ctrlIn.stopCmd = false;
    Serial.println(F("OK"));
  } else if (!strcasecmp(cmd, "STOPSEQ")) {
    ctrlIn.stopCmd = true;
    Serial.println(F("OK"));
  } else if (!strcasecmp(cmd, "ACK")) {
    ctrlIn.ack = true;
    Serial.println(F("OK"));
  } else if (!strcasecmp(cmd, "RESET")) {
    ctrlIn.reset = true;
    Serial.println(F("OK"));
  } else if (!strcasecmp(cmd, "FULLV")) {
    char *arg = strtok(nullptr, " \t\r\n");
    ctrlIn.fullVoltage = arg && (!strcasecmp(arg, "ON") || !strcmp(arg, "1"));
    Serial.println(F("OK"));
  } else if (!strcasecmp(cmd, "THERMAL")) {
    char *arg = strtok(nullptr, " \t\r\n");
    ctrlIn.thermalOk = arg && (!strcasecmp(arg, "ON") || !strcmp(arg, "1"));
    Serial.println(F("OK"));
  } else if (!strcasecmp(cmd, "SCENARIO")) {
    char *arg = strtok(nullptr, " \t\r\n");
    ScenarioMode nextScenario;
    if (arg && parseScenario(arg, nextScenario)) {
      scenarioMode = nextScenario;
      faultCode = FAULT_NONE;
      fault = false;
      ctrlIn.startCmd = false;
      ctrlIn.stopCmd = false;
      ctrlIn.ack = false;
      ctrlIn.reset = false;
      applyScenarioDefaults();
      resetProcessSim();
      setControlState(CTRL_IDLE);
      Serial.println(F("OK"));
    } else {
      Serial.println(F("ERR SCENARIO expects NORMAL, HEAVY_LOAD, NO_DISCHARGE, NO_FIELD, LOW_PF, THERMAL_TRIP, PULLOUT, FULLV_MISSING"));
    }
  } else if (!strcasecmp(cmd, "RUN")) {
    char *arg = strtok(nullptr, " \t\r\n");
    motorRun = arg && (!strcasecmp(arg, "ON") || !strcmp(arg, "1"));
    Serial.println(F("OK"));
  } else if (!strcasecmp(cmd, "AUTO")) {
    char *arg = strtok(nullptr, " \t\r\n");
    autoField = arg && (!strcasecmp(arg, "ON") || !strcmp(arg, "1"));
    Serial.println(F("OK"));
  } else if (!strcasecmp(cmd, "FIELD")) {
    char *arg = strtok(nullptr, " \t\r\n");
    if (arg) manualFieldDuty = clampf(atof(arg), 0.0f, 0.95f);
    Serial.println(F("OK"));
  } else if (!strcasecmp(cmd, "VSET")) {
    char *arg = strtok(nullptr, " \t\r\n");
    if (arg) voltageSetpoint = clampf(atof(arg), 10.0f, 240.0f);
    Serial.println(F("OK"));
  } else if (!strcasecmp(cmd, "SPI")) {
    char *arg = strtok(nullptr, " \t\r\n");
    const uint32_t hz = arg ? (uint32_t)atol(arg) : 0;
    if (hz >= 500000u && hz <= 12000000u) {
      adsSpiHz = hz;
      adsSpiSettings = SPISettings(adsSpiHz, MSBFIRST, SPI_MODE0);
      resetLinkStats();
      Serial.println(F("OK"));
    } else {
      Serial.println(F("ERR SPI expects 500000..12000000"));
    }
  } else if (!strcasecmp(cmd, "RESETSTATS")) {
    resetLinkStats();
    Serial.println(F("OK"));
  } else if (!strcasecmp(cmd, "CONFIG")) {
    printConfig();
  } else if (!strcasecmp(cmd, "STATUS")) {
    printTelemetry();
  } else {
    Serial.println(F("ERR unknown command"));
  }
}

static void serviceSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialLineLen > 0) {
        serialLine[serialLineLen] = '\0';
        handleCommand(serialLine);
        serialLineLen = 0;
      }
    } else if (serialLineLen < sizeof(serialLine) - 1) {
      serialLine[serialLineLen++] = c;
    }
  }
}

// ------------------------------ ARDUINO --------------------------------

void setup() {
  Serial.begin(115200);
  pinMode(PIN_ADS_CS, OUTPUT);
  pinMode(PIN_ADS_DRDY, INPUT_PULLUP);
  pinMode(PIN_MOTOR_RUN, OUTPUT);
  pinMode(PIN_FIELD_ENABLE, OUTPUT);
  pinMode(PIN_SYNC_PULSE, OUTPUT);
  pinMode(PIN_FAULT, OUTPUT);
  pinMode(PIN_HEARTBEAT_LED, OUTPUT);
  pinMode(PIN_FB_BREAKER_CLOSED, INPUT_PULLDOWN);
  pinMode(PIN_FB_SPEED_OK, INPUT_PULLDOWN);
  pinMode(PIN_FB_FIELD_CURRENT, INPUT_PULLDOWN);
  pinMode(PIN_FB_DISCHARGE_CURRENT, INPUT_PULLDOWN);
  pinMode(PIN_FB_THERMAL_OK, INPUT_PULLDOWN);
  pinMode(PIN_FB_EXCITER_READY, INPUT_PULLDOWN);
  pinMode(PIN_FB_LOAD_READY, INPUT_PULLDOWN);
  pinMode(PIN_FB_EMERGENCY_OK, INPUT);
  pinMode(PIN_FB_PLANT_FAULT, INPUT);
  digitalWrite(PIN_ADS_CS, HIGH);
  digitalWrite(PIN_MOTOR_RUN, LOW);
  digitalWrite(PIN_FIELD_ENABLE, LOW);
  digitalWrite(PIN_SYNC_PULSE, LOW);
  digitalWrite(PIN_FAULT, LOW);
  digitalWrite(PIN_HEARTBEAT_LED, HIGH);

  configureFieldPwm();
  adsSpi.begin(PIN_ADS_SCLK, PIN_ADS_MISO, PIN_ADS_MOSI, PIN_ADS_CS);
  FrequencyPllConfig pllCfg;
  pllCfg.sampleRate = NOMINAL_SAMPLE_RATE;
  pllCfg.nominalFreqHz = 60.0f;
  pllCfg.minFreqHz = 45.0f;
  pllCfg.maxFreqHz = 65.0f;
  pllCfg.pllBandwidthHz = 5.0f;
  pllCfg.damping = 0.707f;
  pllCfg.updateSamples = 32;
  if (!initFrequency(pllCfg)) {
    Serial.println(F("Frequency PLL init failed"));
  }
  BaseType_t taskOk = xTaskCreatePinnedToCore(
      adsAcquisitionTask,
      "ads_acq",
      ADS_TASK_STACK_WORDS,
      nullptr,
      ADS_TASK_PRIORITY,
      &adsTaskHandle,
      ADS_TASK_CORE);
  if (taskOk != pdPASS) {
    Serial.println(F("ADS acquisition task init failed"));
  }
  attachInterrupt(digitalPinToInterrupt(PIN_ADS_DRDY), onAdsDrdyFalling, FALLING);

  lastStatsMs = millis();
  lastTelemetryMs = millis();
  printConfig();
}

void loop() {
  serviceSerial();
  updateScrGateCommands();

  if (syncPulseReleaseUs != 0 && (int32_t)(micros() - syncPulseReleaseUs) >= 0) {
    digitalWrite(PIN_SYNC_PULSE, LOW);
    syncPulseReleaseUs = 0;
  }

  const uint32_t now = millis();
  updateHeartbeatLed(now);
  if (now - lastTelemetryMs >= TELEMETRY_MS) {
    publishWindowMeasurements();
    updateSequenceControl();
    updateMotorControl();
    updateStats();
    printTelemetry();
    lastTelemetryMs = now;
  }
}
