#include <Arduino.h>
#include <esp_system.h>
#include <math.h>

namespace {
constexpr uint32_t kBaud = 115200;
constexpr uint32_t kUpdatePeriodUs = 1000;
constexpr uint32_t kPulseWidthUs = 1200;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr int32_t kAdcPositiveFullScale = 8388607;

constexpr uint8_t kPinZeroCross = 18;
constexpr uint8_t kPinHallA = 19;
constexpr uint8_t kPinHallB = 21;
constexpr uint8_t kPinHallC = 22;
constexpr uint8_t kPinIndex = 23;
constexpr uint8_t kPinFault = 25;
constexpr uint8_t kPinGateWindow = 26;

bool enabled = false;
bool fault = false;
bool zeroCrossPulse = false;
bool indexPulse = false;
bool gateWindow = false;

float thetaDeg = 0.0f;
float speedDegPerSec = 0.0f;
float targetSpeedDegPerSec = 30.0f;
float accelDegPerSec2 = 180.0f;
float angleSetpointDeg = 90.0f;
float lineFrequencyHz = 1.0f;
float voltagePeak = 170.0f;
float currentPeak = 10.0f;
float dcBusVolts = 325.0f;
float temperatureC = 32.0f;
float powerFactorAngleDeg = 25.0f;
float measurementNoise = 0.002f;
float adsFullScaleVolts[8] = {
    250.0f, 250.0f, 250.0f, 25.0f, 25.0f, 25.0f, 500.0f, 150.0f,
};

uint32_t lastUpdateUs = 0;
uint32_t lastStreamMs = 0;
uint32_t streamPeriodMs = 20;
uint32_t zeroCrossUntilUs = 0;
uint32_t indexUntilUs = 0;
float lineThetaDeg = 0.0f;
float motorVoltage[3] = {};
float adsEng[8] = {};
int32_t adsRaw[8] = {};

String rxLine;

float clampFloat(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

float wrap360(float value) {
  while (value >= 360.0f) value -= 360.0f;
  while (value < 0.0f) value += 360.0f;
  return value;
}

float noiseTerm(float scale) {
  return scale * measurementNoise * (static_cast<float>(random(-1000, 1001)) / 1000.0f);
}

int32_t engineeringToAdcCode(float value, float fullScale) {
  const float normalized = clampFloat(value / fullScale, -0.999999f, 0.999999f);
  return static_cast<int32_t>(normalized * kAdcPositiveFullScale);
}

void updateAdsModel(float dt) {
  lineThetaDeg = wrap360(lineThetaDeg + 360.0f * lineFrequencyHz * dt);
  const float a = lineThetaDeg * kDegToRad;
  const float b = (lineThetaDeg - 120.0f) * kDegToRad;
  const float c = (lineThetaDeg + 120.0f) * kDegToRad;
  const float iShift = powerFactorAngleDeg * kDegToRad;
  const float loadScale = enabled && !fault ? clampFloat(fabsf(speedDegPerSec) / 360.0f, 0.05f, 1.0f) : 0.0f;

  adsEng[0] = voltagePeak * sinf(a) + noiseTerm(voltagePeak);
  adsEng[1] = voltagePeak * sinf(b) + noiseTerm(voltagePeak);
  adsEng[2] = voltagePeak * sinf(c) + noiseTerm(voltagePeak);

  const float phaseAngles[3] = {lineThetaDeg, wrap360(lineThetaDeg - 120.0f), wrap360(lineThetaDeg + 120.0f)};
  for (uint8_t i = 0; i < 3; ++i) {
    const float halfCycleAngle = (phaseAngles[i] >= 180.0f) ? phaseAngles[i] - 180.0f : phaseAngles[i];
    const bool scrConducting = enabled && !fault && halfCycleAngle >= angleSetpointDeg;
    motorVoltage[i] = scrConducting ? adsEng[i] : 0.0f;
  }

  adsEng[3] = currentPeak * loadScale * sinf(a - iShift) + noiseTerm(currentPeak);
  adsEng[4] = currentPeak * loadScale * sinf(b - iShift) + noiseTerm(currentPeak);
  adsEng[5] = currentPeak * loadScale * sinf(c - iShift) + noiseTerm(currentPeak);
  adsEng[6] = dcBusVolts + 4.0f * sinf(2.0f * a) + noiseTerm(dcBusVolts);
  adsEng[7] = temperatureC + 0.02f * fabsf(speedDegPerSec) + noiseTerm(temperatureC);

  for (uint8_t i = 0; i < 8; ++i) {
    adsRaw[i] = engineeringToAdcCode(adsEng[i], adsFullScaleVolts[i]);
  }
}

void writeHall(uint8_t sector) {
  // 120-degree Hall sequence for a positive electrical rotation.
  static constexpr uint8_t hallTable[6] = {
      0b001, 0b101, 0b100, 0b110, 0b010, 0b011,
  };
  const uint8_t hall = hallTable[sector % 6];
  digitalWrite(kPinHallA, (hall & 0b001) ? HIGH : LOW);
  digitalWrite(kPinHallB, (hall & 0b010) ? HIGH : LOW);
  digitalWrite(kPinHallC, (hall & 0b100) ? HIGH : LOW);
}

uint8_t hallValue(uint8_t sector) {
  static constexpr uint8_t hallTable[6] = {
      0b001, 0b101, 0b100, 0b110, 0b010, 0b011,
  };
  return hallTable[sector % 6];
}

uint8_t sectorFromTheta(float theta) {
  return static_cast<uint8_t>(theta / 60.0f) % 6;
}

void printHelp() {
  Serial.println(F("OK commands: HELP, ENABLE 1|0, SPEED deg_per_s, ACCEL deg_per_s2, ANGLE 0..180, FAULT 1|0, STREAM ms, LINEHZ hz, VPEAK v, IPEAK a, VDC v, TEMP c, PFDEG deg, NOISE frac, STATUS"));
}

void printStatus() {
  const uint8_t sector = sectorFromTheta(thetaDeg);
  const uint8_t hall = hallValue(sector);
  Serial.print(F("TEL t_ms="));
  Serial.print(millis());
  Serial.print(F(" en="));
  Serial.print(enabled ? 1 : 0);
  Serial.print(F(" fault="));
  Serial.print(fault ? 1 : 0);
  Serial.print(F(" theta="));
  Serial.print(thetaDeg, 2);
  Serial.print(F(" speed="));
  Serial.print(speedDegPerSec, 2);
  Serial.print(F(" target="));
  Serial.print(targetSpeedDegPerSec, 2);
  Serial.print(F(" accel="));
  Serial.print(accelDegPerSec2, 2);
  Serial.print(F(" set="));
  Serial.print(angleSetpointDeg, 2);
  Serial.print(F(" sector="));
  Serial.print(sector);
  Serial.print(F(" hall="));
  Serial.print(hall & 1);
  Serial.print((hall >> 1) & 1);
  Serial.print((hall >> 2) & 1);
  Serial.print(F(" gate="));
  Serial.print(gateWindow ? 1 : 0);
  Serial.print(F(" zc="));
  Serial.print(zeroCrossPulse ? 1 : 0);
  Serial.print(F(" idx="));
  Serial.print(indexPulse ? 1 : 0);
  Serial.print(F(" va="));
  Serial.print(adsEng[0], 2);
  Serial.print(F(" vb="));
  Serial.print(adsEng[1], 2);
  Serial.print(F(" vc="));
  Serial.print(adsEng[2], 2);
  Serial.print(F(" vma="));
  Serial.print(motorVoltage[0], 2);
  Serial.print(F(" vmb="));
  Serial.print(motorVoltage[1], 2);
  Serial.print(F(" vmc="));
  Serial.print(motorVoltage[2], 2);
  Serial.print(F(" ia="));
  Serial.print(adsEng[3], 3);
  Serial.print(F(" ib="));
  Serial.print(adsEng[4], 3);
  Serial.print(F(" ic="));
  Serial.print(adsEng[5], 3);
  Serial.print(F(" vdc="));
  Serial.print(adsEng[6], 2);
  Serial.print(F(" temp="));
  Serial.print(adsEng[7], 2);
  Serial.print(F(" adc0="));
  Serial.print(adsRaw[0]);
  Serial.print(F(" adc1="));
  Serial.print(adsRaw[1]);
  Serial.print(F(" adc2="));
  Serial.print(adsRaw[2]);
  Serial.print(F(" adc3="));
  Serial.print(adsRaw[3]);
  Serial.print(F(" adc4="));
  Serial.print(adsRaw[4]);
  Serial.print(F(" adc5="));
  Serial.print(adsRaw[5]);
  Serial.print(F(" adc6="));
  Serial.print(adsRaw[6]);
  Serial.print(F(" adc7="));
  Serial.println(adsRaw[7]);
}

void acknowledge(const __FlashStringHelper *message) {
  Serial.print(F("OK "));
  Serial.println(message);
}

void handleCommand(String line) {
  line.trim();
  line.toUpperCase();
  if (line.length() == 0) return;

  const int space = line.indexOf(' ');
  const String cmd = (space >= 0) ? line.substring(0, space) : line;
  const String arg = (space >= 0) ? line.substring(space + 1) : "";

  if (cmd == "HELP") {
    printHelp();
  } else if (cmd == "ENABLE") {
    enabled = arg.toInt() != 0;
    acknowledge(F("ENABLE"));
  } else if (cmd == "SPEED") {
    targetSpeedDegPerSec = clampFloat(arg.toFloat(), -20000.0f, 20000.0f);
    acknowledge(F("SPEED"));
  } else if (cmd == "ACCEL") {
    accelDegPerSec2 = clampFloat(arg.toFloat(), 1.0f, 100000.0f);
    acknowledge(F("ACCEL"));
  } else if (cmd == "ANGLE") {
    angleSetpointDeg = clampFloat(arg.toFloat(), 0.0f, 180.0f);
    acknowledge(F("ANGLE"));
  } else if (cmd == "FAULT") {
    fault = arg.toInt() != 0;
    acknowledge(F("FAULT"));
  } else if (cmd == "STREAM") {
    streamPeriodMs = static_cast<uint32_t>(clampFloat(arg.toFloat(), 10.0f, 5000.0f));
    acknowledge(F("STREAM"));
  } else if (cmd == "LINEHZ") {
    lineFrequencyHz = clampFloat(arg.toFloat(), 0.1f, 400.0f);
    acknowledge(F("LINEHZ"));
  } else if (cmd == "VPEAK") {
    voltagePeak = clampFloat(arg.toFloat(), 0.0f, 240.0f);
    acknowledge(F("VPEAK"));
  } else if (cmd == "IPEAK") {
    currentPeak = clampFloat(arg.toFloat(), 0.0f, 24.0f);
    acknowledge(F("IPEAK"));
  } else if (cmd == "VDC") {
    dcBusVolts = clampFloat(arg.toFloat(), 0.0f, 480.0f);
    acknowledge(F("VDC"));
  } else if (cmd == "TEMP") {
    temperatureC = clampFloat(arg.toFloat(), -40.0f, 125.0f);
    acknowledge(F("TEMP"));
  } else if (cmd == "PFDEG") {
    powerFactorAngleDeg = clampFloat(arg.toFloat(), -89.0f, 89.0f);
    acknowledge(F("PFDEG"));
  } else if (cmd == "NOISE") {
    measurementNoise = clampFloat(arg.toFloat(), 0.0f, 0.05f);
    acknowledge(F("NOISE"));
  } else if (cmd == "STATUS") {
    printStatus();
  } else {
    Serial.print(F("ERR unknown_command "));
    Serial.println(cmd);
  }
}

void readSerial() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (rxLine.length() > 0) {
        handleCommand(rxLine);
        rxLine = "";
      }
    } else if (rxLine.length() < 128) {
      rxLine += c;
    }
  }
}

void updateSimulation(uint32_t nowUs) {
  if (lastUpdateUs == 0) {
    lastUpdateUs = nowUs;
    return;
  }

  const uint32_t elapsedUs = nowUs - lastUpdateUs;
  if (elapsedUs < kUpdatePeriodUs) return;
  lastUpdateUs = nowUs;

  const float dt = elapsedUs / 1000000.0f;
  const float previousTheta = thetaDeg;
  const float desiredSpeed = (enabled && !fault) ? targetSpeedDegPerSec : 0.0f;
  const float maxDelta = accelDegPerSec2 * dt;

  if (speedDegPerSec < desiredSpeed) {
    speedDegPerSec = min(speedDegPerSec + maxDelta, desiredSpeed);
  } else if (speedDegPerSec > desiredSpeed) {
    speedDegPerSec = max(speedDegPerSec - maxDelta, desiredSpeed);
  }

  updateAdsModel(dt);
  thetaDeg = wrap360(thetaDeg + speedDegPerSec * dt);

  const bool crossed180 = (previousTheta < 180.0f && thetaDeg >= 180.0f);
  const bool wrapped = (speedDegPerSec >= 0.0f) ? (thetaDeg < previousTheta) : (thetaDeg > previousTheta);
  if (crossed180 || wrapped) {
    zeroCrossPulse = true;
    zeroCrossUntilUs = nowUs + kPulseWidthUs;
  }
  if (wrapped) {
    indexPulse = true;
    indexUntilUs = nowUs + kPulseWidthUs;
  }

  const float halfCycleTheta = (thetaDeg >= 180.0f) ? thetaDeg - 180.0f : thetaDeg;
  gateWindow = enabled && !fault && (halfCycleTheta >= angleSetpointDeg) && (halfCycleTheta < angleSetpointDeg + 6.0f);

  writeHall(sectorFromTheta(thetaDeg));
}

void updatePins(uint32_t nowUs) {
  if (zeroCrossPulse && static_cast<int32_t>(nowUs - zeroCrossUntilUs) >= 0) {
    zeroCrossPulse = false;
  }
  if (indexPulse && static_cast<int32_t>(nowUs - indexUntilUs) >= 0) {
    indexPulse = false;
  }

  digitalWrite(kPinZeroCross, zeroCrossPulse ? HIGH : LOW);
  digitalWrite(kPinIndex, indexPulse ? HIGH : LOW);
  digitalWrite(kPinFault, fault ? HIGH : LOW);
  digitalWrite(kPinGateWindow, gateWindow ? HIGH : LOW);
}
}  // namespace

void setup() {
  pinMode(kPinZeroCross, OUTPUT);
  pinMode(kPinHallA, OUTPUT);
  pinMode(kPinHallB, OUTPUT);
  pinMode(kPinHallC, OUTPUT);
  pinMode(kPinIndex, OUTPUT);
  pinMode(kPinFault, OUTPUT);
  pinMode(kPinGateWindow, OUTPUT);

  Serial.begin(kBaud);
  randomSeed(esp_random());
  delay(200);
  printHelp();
}

void loop() {
  const uint32_t nowUs = micros();
  readSerial();
  updateSimulation(nowUs);
  updatePins(nowUs);

  const uint32_t nowMs = millis();
  if (nowMs - lastStreamMs >= streamPeriodMs) {
    lastStreamMs = nowMs;
    printStatus();
  }
}
