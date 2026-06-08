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
static const int PIN_SYNC_PULSE = 26;
static const int PIN_FAULT = 33;

static const uint8_t NUM_CHANNELS = 8;
static const uint8_t WORD_BYTES = 3;
static const uint16_t FRAME_BYTES = (1 + NUM_CHANNELS) * WORD_BYTES;
static const uint32_t SPI_HZ = 10000000;
static const uint32_t NOMINAL_SAMPLE_RATE = 8000;
static const uint32_t TELEMETRY_MS = 100;
static const uint32_t MIN_ADS_READ_INTERVAL_US = 112;
static const uint32_t SYNC_PULSE_US = 800;
static const uint32_t FIELD_PWM_HZ = 20000;
static const uint8_t FIELD_PWM_BITS = 10;
static const uint8_t FIELD_PWM_CH = 0;

// ----------------------------- STATE ----------------------------------

struct Measurements {
  float freq_hz = 0.0f;
  float va_rms = 0.0f;
  float vb_rms = 0.0f;
  float vc_rms = 0.0f;
  float ia_rms = 0.0f;
  float ib_rms = 0.0f;
  float ic_rms = 0.0f;
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
  uint32_t count = 0;
};

static SPIClass adsSpi(VSPI);
static SPISettings adsSpiSettings(SPI_HZ, MSBFIRST, SPI_MODE0);

static uint8_t rxFrame[FRAME_BYTES];
static uint8_t txZeros[FRAME_BYTES];
static int32_t chRaw[NUM_CHANNELS];
static float chNorm[NUM_CHANNELS];
static Measurements meas;
static Accumulators accum;

static bool running = true;
static bool motorRun = false;
static bool autoField = true;
static bool fault = false;
static float voltageSetpoint = 0.80f;
static float manualFieldDuty = 0.35f;
static float prevVa = 0.0f;
static uint32_t lastVaCrossUs = 0;
static uint32_t lastAdsReadUs = 0;
static volatile uint16_t drdyPending = 0;
static volatile uint32_t drdyEvents = 0;
static volatile uint32_t drdyDrops = 0;
static uint32_t lastTelemetryMs = 0;
static uint32_t lastStatsMs = 0;
static uint32_t syncPulseReleaseUs = 0;
static uint32_t framesRead = 0;
static uint32_t badFrames = 0;
static uint32_t lastBadFramesControl = 0;
static uint32_t lastFramesRead = 0;
static float fps = 0.0f;

static char serialLine[96];
static uint8_t serialLineLen = 0;

static void IRAM_ATTR onAdsDrdyFalling() {
  if (drdyPending < 64) {
    drdyPending++;
  } else {
    drdyDrops++;
  }
  drdyEvents++;
}

// ----------------------------- HELPERS --------------------------------

static int32_t signExtend24(uint32_t v) {
  v &= 0x00FFFFFFu;
  if (v & 0x00800000u) v |= 0xFF000000u;
  return (int32_t)v;
}

static int32_t readS24(const uint8_t *p) {
  return signExtend24(((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]);
}

static float clampf(float value, float lo, float hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
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

// ----------------------------- ADS INPUT -------------------------------

static bool readAdsFrame() {
  digitalWrite(PIN_ADS_CS, LOW);
  adsSpi.beginTransaction(adsSpiSettings);
  adsSpi.transferBytes(txZeros, rxFrame, FRAME_BYTES);
  adsSpi.endTransaction();
  digitalWrite(PIN_ADS_CS, HIGH);

  const uint32_t status = ((uint32_t)rxFrame[0] << 16) | ((uint32_t)rxFrame[1] << 8) | rxFrame[2];
  if ((status & 0x00FF0000u) != 0x00050000u) {
    badFrames++;
    return false;
  }

  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    chRaw[ch] = readS24(&rxFrame[(1 + ch) * WORD_BYTES]);
    chNorm[ch] = (float)chRaw[ch] / 8388607.0f;
  }
  framesRead++;
  return true;
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

  if (!hasPending) {
    const uint32_t idleUs = micros() - lastAdsReadUs;
    if (framesRead == 0 || idleUs > 5000) {
      if (digitalRead(PIN_ADS_DRDY) == HIGH) return;
      hasPending = true;
    } else {
      return;
    }
  }

  const uint32_t nowReadUs = micros();
  if ((uint32_t)(nowReadUs - lastAdsReadUs) < MIN_ADS_READ_INTERVAL_US) return;
  lastAdsReadUs = nowReadUs;

  if (!readAdsFrame()) return;

  const float va = chNorm[0];
  const float vb = chNorm[1];
  const float vc = chNorm[2];
  const float ia = chNorm[4];
  const float ib = chNorm[5];
  const float ic = chNorm[6];

  accum.va2 += (double)va * va;
  accum.vb2 += (double)vb * vb;
  accum.vc2 += (double)vc * vc;
  accum.ia2 += (double)ia * ia;
  accum.ib2 += (double)ib * ib;
  accum.ic2 += (double)ic * ic;
  accum.p += (double)va * ia + (double)vb * ib + (double)vc * ic;
  accum.count++;

  if (prevVa < 0.0f && va >= 0.0f) {
    const uint32_t nowUs = micros();
    if (lastVaCrossUs != 0) {
      const uint32_t periodUs = nowUs - lastVaCrossUs;
      if (periodUs > 1000 && periodUs < 100000) {
        const float freq = 1000000.0f / (float)periodUs;
        meas.freq_hz = meas.freq_hz <= 1.0f ? freq : (meas.freq_hz * 0.85f + freq * 0.15f);
      }
    }
    lastVaCrossUs = nowUs;
    digitalWrite(PIN_SYNC_PULSE, HIGH);
    syncPulseReleaseUs = nowUs + SYNC_PULSE_US;
  }
  prevVa = va;
}

// -------------------------- MEASURE/CONTROL ----------------------------

static void publishWindowMeasurements() {
  if (accum.count == 0) return;
  const double inv = 1.0 / (double)accum.count;
  meas.va_rms = sqrt(accum.va2 * inv);
  meas.vb_rms = sqrt(accum.vb2 * inv);
  meas.vc_rms = sqrt(accum.vc2 * inv);
  meas.ia_rms = sqrt(accum.ia2 * inv);
  meas.ib_rms = sqrt(accum.ib2 * inv);
  meas.ic_rms = sqrt(accum.ic2 * inv);
  meas.p_total = accum.p * inv;

  const float s = 3.0f * ((meas.va_rms + meas.vb_rms + meas.vc_rms) / 3.0f) *
                  ((meas.ia_rms + meas.ib_rms + meas.ic_rms) / 3.0f);
  meas.pf = s > 0.0001f ? clampf(meas.p_total / s, -1.0f, 1.0f) : 0.0f;
  meas.q_total = sqrtf(max(0.0f, s * s - meas.p_total * meas.p_total));

  const float v_avg = (meas.va_rms + meas.vb_rms + meas.vc_rms) / 3.0f;
  const float v_max_dev = max(max(fabsf(meas.va_rms - v_avg), fabsf(meas.vb_rms - v_avg)),
                              fabsf(meas.vc_rms - v_avg));
  meas.v_unbalance = v_avg > 0.001f ? 100.0f * v_max_dev / v_avg : 0.0f;

  accum = Accumulators{};
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
  fault = meas.v_unbalance > 8.0f || recentBadFrames > 10;
  digitalWrite(PIN_FAULT, fault ? HIGH : LOW);
  digitalWrite(PIN_MOTOR_RUN, (motorRun && !fault) ? HIGH : LOW);
  writeFieldPwm((motorRun && !fault) ? meas.field_duty : 0.0f);
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
  Serial.print(F(" drdy=")); Serial.print((uint32_t)drdyEvents);
  Serial.print(F(" drops=")); Serial.print((uint32_t)drdyDrops);
  Serial.print(F(" pend=")); Serial.print((uint32_t)drdyPending);
  Serial.print(F(" f=")); Serial.print(meas.freq_hz, 3);
  Serial.print(F(" va=")); Serial.print(meas.va_rms, 4);
  Serial.print(F(" vb=")); Serial.print(meas.vb_rms, 4);
  Serial.print(F(" vc=")); Serial.print(meas.vc_rms, 4);
  Serial.print(F(" ia=")); Serial.print(meas.ia_rms, 4);
  Serial.print(F(" ib=")); Serial.print(meas.ib_rms, 4);
  Serial.print(F(" ic=")); Serial.print(meas.ic_rms, 4);
  Serial.print(F(" p=")); Serial.print(meas.p_total, 4);
  Serial.print(F(" q=")); Serial.print(meas.q_total, 4);
  Serial.print(F(" pf=")); Serial.print(meas.pf, 4);
  Serial.print(F(" vunb=")); Serial.print(meas.v_unbalance, 2);
  Serial.print(F(" field=")); Serial.print(meas.field_duty, 3);
  Serial.print(F(" run=")); Serial.print(motorRun ? 1 : 0);
  Serial.print(F(" auto=")); Serial.print(autoField ? 1 : 0);
  Serial.print(F(" fault=")); Serial.println(fault ? 1 : 0);
}

// ----------------------------- SERIAL ---------------------------------

static void printConfig() {
  Serial.println(F("FakeFPGA ESP32"));
  Serial.print(F("ADS_SPI_HZ=")); Serial.println(SPI_HZ);
  Serial.print(F("NOMINAL_SAMPLE_RATE=")); Serial.println(NOMINAL_SAMPLE_RATE);
  Serial.print(F("FRAME_BYTES=")); Serial.println(FRAME_BYTES);
  Serial.print(F("DRDY_PIN=")); Serial.println(PIN_ADS_DRDY);
  Serial.print(F("MOTOR_RUN_PIN=")); Serial.println(PIN_MOTOR_RUN);
  Serial.print(F("FIELD_PWM_PIN=")); Serial.println(PIN_FIELD_PWM);
  Serial.print(F("SYNC_PULSE_PIN=")); Serial.println(PIN_SYNC_PULSE);
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
    if (arg) voltageSetpoint = clampf(atof(arg), 0.05f, 1.0f);
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
  pinMode(PIN_SYNC_PULSE, OUTPUT);
  pinMode(PIN_FAULT, OUTPUT);
  digitalWrite(PIN_ADS_CS, HIGH);
  digitalWrite(PIN_MOTOR_RUN, LOW);
  digitalWrite(PIN_SYNC_PULSE, LOW);
  digitalWrite(PIN_FAULT, LOW);

  configureFieldPwm();
  adsSpi.begin(PIN_ADS_SCLK, PIN_ADS_MISO, PIN_ADS_MOSI, PIN_ADS_CS);
  attachInterrupt(digitalPinToInterrupt(PIN_ADS_DRDY), onAdsDrdyFalling, FALLING);

  lastStatsMs = millis();
  lastTelemetryMs = millis();
  printConfig();
}

void loop() {
  serviceSerial();
  sampleAdsIfReady();

  if (syncPulseReleaseUs != 0 && (int32_t)(micros() - syncPulseReleaseUs) >= 0) {
    digitalWrite(PIN_SYNC_PULSE, LOW);
    syncPulseReleaseUs = 0;
  }

  const uint32_t now = millis();
  if (now - lastTelemetryMs >= TELEMETRY_MS) {
    publishWindowMeasurements();
    updateMotorControl();
    updateStats();
    printTelemetry();
    lastTelemetryMs = now;
  }
}
