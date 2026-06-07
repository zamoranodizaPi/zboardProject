#include <Arduino.h>
#include <driver/spi_slave.h>
#include <esp_system.h>
#include <math.h>
#include <string.h>

namespace {
constexpr uint32_t kBaud = 115200;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr int32_t kAdcPositiveFullScale = 8388607;
constexpr size_t kFrameBytes = 32;
constexpr uint8_t kCommandSync0 = 0xC3;
constexpr uint8_t kCommandSync1 = 0x3C;

constexpr int kPinMiso = 19;
constexpr int kPinMosi = 23;
constexpr int kPinSclk = 18;
constexpr int kPinCs = 5;
constexpr int kPinDrdy = 4;

bool enabled = true;
bool fault = false;
float lineFrequencyHz = 10.0f;
float sampleRateHz = 800.0f;
float angleSetpointDeg = 90.0f;
float gateWidthDeg = 15.0f;
float voltagePeak = 170.0f;
float currentPeak = 10.0f;
float dcBusVolts = 325.0f;
float temperatureC = 32.0f;
float measurementNoise = 0.001f;
float thetaDeg = 0.0f;
float vin = 0.0f;
float vmot = 0.0f;
float iload = 0.0f;
float gate = 0.0f;
uint16_t sequence = 0;
String rxLine;

WORD_ALIGNED_ATTR uint8_t txFrame[kFrameBytes];
WORD_ALIGNED_ATTR uint8_t rxFrame[kFrameBytes];

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

void putU16(uint8_t *buffer, size_t offset, uint16_t value) {
  buffer[offset] = static_cast<uint8_t>((value >> 8) & 0xff);
  buffer[offset + 1] = static_cast<uint8_t>(value & 0xff);
}

void putI24(uint8_t *buffer, size_t offset, int32_t value) {
  value = constrain(value, -8388608, 8388607);
  const uint32_t packed = static_cast<uint32_t>(value) & 0x00ffffffUL;
  buffer[offset] = static_cast<uint8_t>((packed >> 16) & 0xff);
  buffer[offset + 1] = static_cast<uint8_t>((packed >> 8) & 0xff);
  buffer[offset + 2] = static_cast<uint8_t>(packed & 0xff);
}

uint16_t checksum16(const uint8_t *buffer, size_t length) {
  uint16_t sum = 0;
  for (size_t i = 0; i < length; ++i) {
    sum = static_cast<uint16_t>(sum + buffer[i]);
  }
  return sum;
}

uint16_t readU16(const uint8_t *buffer, size_t offset) {
  return (static_cast<uint16_t>(buffer[offset]) << 8) | buffer[offset + 1];
}

void applyCommandFrame(const uint8_t *buffer) {
  if (buffer[0] != kCommandSync0 || buffer[1] != kCommandSync1) return;
  if (checksum16(buffer, 30) != readU16(buffer, 30)) return;

  const uint8_t flags = buffer[2];
  enabled = (flags & 0x01) != 0;
  fault = (flags & 0x02) != 0;
  sampleRateHz = clampFloat(static_cast<float>(readU16(buffer, 4)), 500.0f, 20000.0f);
  lineFrequencyHz = clampFloat(static_cast<float>(readU16(buffer, 6)) / 100.0f, 0.1f, 400.0f);
  angleSetpointDeg = clampFloat(static_cast<float>(readU16(buffer, 8)) / 100.0f, 0.0f, 180.0f);
  gateWidthDeg = clampFloat(static_cast<float>(readU16(buffer, 10)) / 100.0f, 1.0f, 45.0f);
  voltagePeak = clampFloat(static_cast<float>(readU16(buffer, 12)) / 10.0f, 0.0f, 240.0f);
  currentPeak = clampFloat(static_cast<float>(readU16(buffer, 14)) / 100.0f, 0.0f, 24.0f);
  dcBusVolts = clampFloat(static_cast<float>(readU16(buffer, 16)) / 10.0f, 0.0f, 480.0f);
  measurementNoise = clampFloat(static_cast<float>(readU16(buffer, 18)) / 100000.0f, 0.0f, 0.05f);
}

void updateModel() {
  const float dt = 1.0f / sampleRateHz;
  thetaDeg = wrap360(thetaDeg + 360.0f * lineFrequencyHz * dt);
  const float phaseRad = thetaDeg * kDegToRad;
  const float halfCycleAngle = (thetaDeg >= 180.0f) ? thetaDeg - 180.0f : thetaDeg;
  const bool conducting = enabled && !fault && halfCycleAngle >= angleSetpointDeg;
  const bool gateActive = enabled && !fault && halfCycleAngle >= angleSetpointDeg && halfCycleAngle < angleSetpointDeg + gateWidthDeg;

  vin = voltagePeak * sinf(phaseRad) + noiseTerm(voltagePeak);
  vmot = conducting ? vin : 0.0f;
  iload = conducting ? currentPeak * sinf(phaseRad - 20.0f * kDegToRad) + noiseTerm(currentPeak) : noiseTerm(currentPeak);
  gate = gateActive ? 1.0f : 0.0f;
}

void buildFrame() {
  updateModel();

  uint16_t status = 0;
  if (enabled) status |= 0x0001;
  if (fault) status |= 0x0002;
  if (gate > 0.5f) status |= 0x0004;

  memset(txFrame, 0, sizeof(txFrame));
  txFrame[0] = 0xA5;
  txFrame[1] = 0x5A;
  putU16(txFrame, 2, sequence++);
  putU16(txFrame, 4, status);
  putI24(txFrame, 6, engineeringToAdcCode(vin, 250.0f));
  putI24(txFrame, 9, engineeringToAdcCode(vmot, 250.0f));
  putI24(txFrame, 12, engineeringToAdcCode(iload, 25.0f));
  putI24(txFrame, 15, engineeringToAdcCode(dcBusVolts, 500.0f));
  putI24(txFrame, 18, engineeringToAdcCode(gate, 1.0f));
  putI24(txFrame, 21, engineeringToAdcCode(thetaDeg - 180.0f, 180.0f));
  putI24(txFrame, 24, engineeringToAdcCode(temperatureC, 150.0f));
  putI24(txFrame, 27, fault ? kAdcPositiveFullScale : 0);
  putU16(txFrame, 30, checksum16(txFrame, 30));

  digitalWrite(kPinDrdy, LOW);
}

void printHelp() {
  Serial.println(F("OK commands: HELP, ENABLE 1|0, FAULT 1|0, ANGLE 0..180, GATEDEG 1..45, LINEHZ hz, FS samples_per_s, VPEAK v, IPEAK a, VDC v, TEMP c, NOISE frac, STATUS"));
}

void printStatus() {
  Serial.print(F("TEL en="));
  Serial.print(enabled ? 1 : 0);
  Serial.print(F(" fault="));
  Serial.print(fault ? 1 : 0);
  Serial.print(F(" theta="));
  Serial.print(thetaDeg, 2);
  Serial.print(F(" angle="));
  Serial.print(angleSetpointDeg, 2);
  Serial.print(F(" gatedeg="));
  Serial.print(gateWidthDeg, 2);
  Serial.print(F(" linehz="));
  Serial.print(lineFrequencyHz, 2);
  Serial.print(F(" fs="));
  Serial.print(sampleRateHz, 0);
  Serial.print(F(" vin="));
  Serial.print(vin, 2);
  Serial.print(F(" vmot="));
  Serial.print(vmot, 2);
  Serial.print(F(" iload="));
  Serial.print(iload, 3);
  Serial.print(F(" gate="));
  Serial.println(gate > 0.5f ? 1 : 0);
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
  } else if (cmd == "FAULT") {
    fault = arg.toInt() != 0;
    acknowledge(F("FAULT"));
  } else if (cmd == "ANGLE") {
    angleSetpointDeg = clampFloat(arg.toFloat(), 0.0f, 180.0f);
    acknowledge(F("ANGLE"));
  } else if (cmd == "GATEDEG") {
    gateWidthDeg = clampFloat(arg.toFloat(), 1.0f, 45.0f);
    acknowledge(F("GATEDEG"));
  } else if (cmd == "LINEHZ") {
    lineFrequencyHz = clampFloat(arg.toFloat(), 0.1f, 400.0f);
    acknowledge(F("LINEHZ"));
  } else if (cmd == "FS") {
    sampleRateHz = clampFloat(arg.toFloat(), 500.0f, 20000.0f);
    acknowledge(F("FS"));
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

void setupSpiSlave() {
  spi_bus_config_t busConfig = {};
  busConfig.mosi_io_num = kPinMosi;
  busConfig.miso_io_num = kPinMiso;
  busConfig.sclk_io_num = kPinSclk;
  busConfig.quadwp_io_num = -1;
  busConfig.quadhd_io_num = -1;
  busConfig.max_transfer_sz = kFrameBytes;

  spi_slave_interface_config_t slaveConfig = {};
  slaveConfig.spics_io_num = kPinCs;
  slaveConfig.flags = 0;
  slaveConfig.queue_size = 1;
  slaveConfig.mode = 1;

  const esp_err_t err = spi_slave_initialize(VSPI_HOST, &busConfig, &slaveConfig, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    Serial.print(F("ERR spi_slave_initialize "));
    Serial.println(static_cast<int>(err));
    while (true) {
      delay(1000);
    }
  }
}
}  // namespace

void setup() {
  pinMode(kPinDrdy, OUTPUT);
  digitalWrite(kPinDrdy, HIGH);
  Serial.begin(kBaud);
  randomSeed(esp_random());
  delay(200);
  printHelp();
  setupSpiSlave();
}

void loop() {
  readSerial();
  buildFrame();

  spi_slave_transaction_t transaction = {};
  transaction.length = kFrameBytes * 8;
  transaction.tx_buffer = txFrame;
  transaction.rx_buffer = rxFrame;

  const esp_err_t err = spi_slave_transmit(VSPI_HOST, &transaction, pdMS_TO_TICKS(100));
  digitalWrite(kPinDrdy, HIGH);
  if (err == ESP_OK) {
    applyCommandFrame(rxFrame);
  }
  if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
    Serial.print(F("ERR spi_slave_transmit "));
    Serial.println(static_cast<int>(err));
    delay(100);
  }
}
