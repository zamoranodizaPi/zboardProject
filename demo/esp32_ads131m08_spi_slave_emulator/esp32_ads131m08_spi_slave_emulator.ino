/*
  ESP32-WROOM-32 ADS131M08 streaming emulator

  Arduino sketch that makes an ESP32 act as an SPI slave that streams frames
  shaped like an ADS131M08 data frame:

    STATUS_WORD, CH0, CH1, CH2, CH3, CH4, CH5, CH6, CH7, optional CRC

  This first version focuses on deterministic sample streaming and DRDY timing.
  It intentionally does not implement real ADS131M08 register commands such as
  RESET, WREG, or RREG.

  Default pin table:
    CS    GPIO 5   input, SPI chip select
    SCLK  GPIO 18  input, SPI clock
    MOSI  GPIO 23  input, master-to-slave
    MISO  GPIO 19  output, slave-to-master
    DRDY  GPIO 4   output, active-low by default

  Serial commands:
    BITS 16|24|32
    RATE 1000..32000
    SPI 5000000
    SPIMODE 0|1|2|3
    CRC ON|OFF
    MODE CONSTANT|COUNTER|SINE|TRIANGLE|RANDOM
    CH <0..7> CONSTANT|COUNTER|SINE|TRIANGLE|RANDOM [value]
    START
    STOP
    CONFIG
    STATS
*/

#include <Arduino.h>
#include <strings.h>
#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "esp_err.h"

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

#ifndef DMA_ATTR
#define DMA_ATTR
#endif

#ifndef SPI_DMA_CH_AUTO
#define SPI_DMA_CH_AUTO 1
#endif

#ifndef TWO_PI
#define TWO_PI 6.28318530717958647692f
#endif

// ----------------------------- CONFIG ---------------------------------

static const int PIN_SPI_CS = 5;
static const int PIN_SPI_SCLK = 18;
static const int PIN_SPI_MOSI = 23;
static const int PIN_SPI_MISO = 19;

static const int DRDY_PIN = 4;
static const bool DRDY_ACTIVE_LOW = true;
static const bool DRDY_PULSE_MODE = true;     // false = level held until a frame is sent
static const uint32_t DRDY_PULSE_US = 8;

static const uint8_t NUM_CHANNELS = 8;
static const uint8_t SPI_QUEUE_DEPTH = 4;
static const uint8_t MAX_WORD_BYTES = 4;
static const uint8_t MAX_FRAME_WORDS = NUM_CHANNELS + 2; // status + 8 channels + crc
static const uint16_t MAX_FRAME_BYTES = MAX_FRAME_WORDS * MAX_WORD_BYTES;
static const uint32_t TIMER_BASE_HZ = 8000000;

static uint8_t WORD_LENGTH = 24;              // 16, 24, or 32
static uint32_t SAMPLE_RATE = 4000;           // 1000 to 32000 SPS
static uint32_t SPI_CLOCK_MAX_HZ = 10000000;  // informational limit for the master
static uint8_t SPI_MODE = 0;                  // 0 to 3
static bool ENABLE_CRC = false;

static const float DEFAULT_SINE_FREQ_HZ = 60.0f;
static const float DEFAULT_AMPLITUDE = 0.8f;  // fraction of 24-bit full-scale
static const int32_t DEFAULT_OFFSET = 0;
static const int32_t DEFAULT_CONSTANT = 1000;

// ----------------------------- TYPES ----------------------------------

enum SignalMode : uint8_t {
  SIGNAL_CONSTANT = 0,
  SIGNAL_COUNTER,
  SIGNAL_SINE,
  SIGNAL_TRIANGLE,
  SIGNAL_RANDOM
};

struct ChannelConfig {
  SignalMode mode;
  int32_t constantValue;
  float sineFreqHz;
  float amplitude;
  int32_t offset;
  uint32_t phase;       // 32-bit phase accumulator
  uint32_t phaseStep;
  int32_t counter;
};

struct RuntimeStats {
  volatile uint32_t sampleTicks;
  volatile uint32_t overruns;
  volatile uint32_t framesSent;
  volatile uint32_t spiFrames;
  volatile bool running;
  volatile bool sampleDue;
  volatile bool drdyAsserted;
  uint32_t lastStatsMs;
  uint32_t lastFramesSent;
  uint32_t lastSampleTicks;
  uint32_t buildMicrosAccum;
  float fps;
  float sampleFps;
  float cpuPct;
};

// ----------------------------- STATE ----------------------------------

static ChannelConfig channels[NUM_CHANNELS];
static RuntimeStats stats;

static uint8_t currentFrame[MAX_FRAME_BYTES];
static volatile uint16_t currentFrameBytes = 0;
static volatile uint32_t frameSequence = 0;
static volatile uint32_t drdyReleaseAtUs = 0;

DMA_ATTR static uint8_t txBuffers[SPI_QUEUE_DEPTH][MAX_FRAME_BYTES];
DMA_ATTR static uint8_t rxBuffers[SPI_QUEUE_DEPTH][MAX_FRAME_BYTES];
static spi_slave_transaction_t spiTransactions[SPI_QUEUE_DEPTH];
static bool spiStarted = false;

static char serialLine[96];
static uint8_t serialLineLen = 0;

static hw_timer_t *sampleTimer = nullptr;
static portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// -------------------------- SMALL HELPERS -----------------------------

static inline uint8_t wordBytes() {
  return WORD_LENGTH / 8;
}

static inline uint16_t frameBytes() {
  return (uint16_t)((1 + NUM_CHANNELS + (ENABLE_CRC ? 1 : 0)) * wordBytes());
}

static inline int drdyActiveLevel() {
  return DRDY_ACTIVE_LOW ? LOW : HIGH;
}

static inline int drdyInactiveLevel() {
  return DRDY_ACTIVE_LOW ? HIGH : LOW;
}

static const char *signalName(SignalMode mode) {
  switch (mode) {
    case SIGNAL_CONSTANT: return "CONSTANT";
    case SIGNAL_COUNTER: return "COUNTER";
    case SIGNAL_SINE: return "SINE";
    case SIGNAL_TRIANGLE: return "TRIANGLE";
    case SIGNAL_RANDOM: return "RANDOM";
    default: return "UNKNOWN";
  }
}

static bool parseSignalMode(const char *text, SignalMode &mode) {
  if (!strcasecmp(text, "CONSTANT")) mode = SIGNAL_CONSTANT;
  else if (!strcasecmp(text, "COUNTER")) mode = SIGNAL_COUNTER;
  else if (!strcasecmp(text, "SINE") || !strcasecmp(text, "SENO")) mode = SIGNAL_SINE;
  else if (!strcasecmp(text, "TRIANGLE") || !strcasecmp(text, "RAMPA")) mode = SIGNAL_TRIANGLE;
  else if (!strcasecmp(text, "RANDOM")) mode = SIGNAL_RANDOM;
  else return false;
  return true;
}

static int32_t clamp24(int64_t value) {
  if (value > 0x7FFFFF) return 0x7FFFFF;
  if (value < -0x800000) return -0x800000;
  return (int32_t)value;
}

static uint32_t xorshift32() {
  static uint32_t seed = 0xA5A5F00Du;
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

static void updateChannelPhaseSteps() {
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    channels[ch].phaseStep = (uint32_t)((channels[ch].sineFreqHz * 4294967296.0f) / (float)SAMPLE_RATE);
  }
}

// ------------------------- SAMPLE GENERATION --------------------------

static int32_t generateChannelSample(uint8_t chIndex) {
  ChannelConfig &ch = channels[chIndex];
  const int32_t fullScale = 0x7FFFFF;
  const int32_t amp = clamp24((int64_t)(ch.amplitude * (float)fullScale));

  switch (ch.mode) {
    case SIGNAL_CONSTANT:
      return clamp24((int64_t)ch.constantValue + ch.offset);

    case SIGNAL_COUNTER:
      ch.counter += 1 + chIndex;
      return clamp24((int64_t)ch.counter + ch.offset);

    case SIGNAL_SINE: {
      ch.phase += ch.phaseStep;
      const float radians = ((float)ch.phase / 4294967296.0f) * TWO_PI;
      return clamp24((int64_t)(sinf(radians) * (float)amp) + ch.offset);
    }

    case SIGNAL_TRIANGLE: {
      ch.phase += ch.phaseStep;
      uint32_t p = ch.phase;
      int32_t tri = (p < 0x80000000u)
                      ? (int32_t)(p >> 7) - 0x800000
                      : 0x7FFFFF - (int32_t)((p - 0x80000000u) >> 7);
      return clamp24(((int64_t)tri * amp) / fullScale + ch.offset);
    }

    case SIGNAL_RANDOM:
      return clamp24((int32_t)(xorshift32() & 0x00FFFFFFu) - 0x800000 + ch.offset);
  }

  return 0;
}

// ---------------------------- FRAME -----------------------------------

static uint16_t crc16Ccitt(const uint8_t *data, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

static void packWord(uint8_t *dst, int32_t sample24) {
  if (WORD_LENGTH == 16) {
    int16_t s16 = (int16_t)(sample24 >> 8);
    dst[0] = (uint8_t)((s16 >> 8) & 0xFF);
    dst[1] = (uint8_t)(s16 & 0xFF);
  } else if (WORD_LENGTH == 24) {
    uint32_t v = (uint32_t)sample24 & 0x00FFFFFFu;
    dst[0] = (uint8_t)((v >> 16) & 0xFF);
    dst[1] = (uint8_t)((v >> 8) & 0xFF);
    dst[2] = (uint8_t)(v & 0xFF);
  } else {
    int32_t s32 = sample24 * 256; // sign-extended 24-bit value aligned into 32-bit word
    dst[0] = (uint8_t)((s32 >> 24) & 0xFF);
    dst[1] = (uint8_t)((s32 >> 16) & 0xFF);
    dst[2] = (uint8_t)((s32 >> 8) & 0xFF);
    dst[3] = (uint8_t)(s32 & 0xFF);
  }
}

static void buildFrame() {
  const uint32_t startUs = micros();
  const uint8_t bytes = wordBytes();
  uint8_t *p = currentFrame;
  uint32_t seq = frameSequence++;

  int32_t status = (int32_t)(0x050000 | (seq & 0xFFFF)); // recognizable status marker + sequence
  packWord(p, status);
  p += bytes;

  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    packWord(p, generateChannelSample(ch));
    p += bytes;
  }

  if (ENABLE_CRC) {
    const uint16_t payloadLen = (uint16_t)(p - currentFrame);
    uint16_t crc = crc16Ccitt(currentFrame, payloadLen);
    int32_t crcWord = (int32_t)crc;
    packWord(p, crcWord);
    p += bytes;
  }

  currentFrameBytes = (uint16_t)(p - currentFrame);
  stats.buildMicrosAccum += micros() - startUs;
}

// ----------------------------- DRDY -----------------------------------

static void IRAM_ATTR onSampleTimer() {
  portENTER_CRITICAL_ISR(&timerMux);
  if (stats.running) {
    if (stats.sampleDue) {
      stats.overruns++;
    }
    stats.sampleTicks++;
    stats.sampleDue = true;
    stats.drdyAsserted = true;
    gpio_set_level((gpio_num_t)DRDY_PIN, drdyActiveLevel());
    if (DRDY_PULSE_MODE) {
      drdyReleaseAtUs = micros() + DRDY_PULSE_US;
    }
  }
  portEXIT_CRITICAL_ISR(&timerMux);
}

static void configureSampleTimer() {
  if (sampleTimer) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    timerEnd(sampleTimer);
#else
    timerEnd(sampleTimer);
#endif
    sampleTimer = nullptr;
  }

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  sampleTimer = timerBegin(TIMER_BASE_HZ);
  timerAttachInterrupt(sampleTimer, &onSampleTimer);
  timerAlarm(sampleTimer, TIMER_BASE_HZ / SAMPLE_RATE, true, 0);
#else
  sampleTimer = timerBegin(0, 10, true); // 8 MHz on 80 MHz APB
  timerAttachInterrupt(sampleTimer, &onSampleTimer, true);
  timerAlarmWrite(sampleTimer, TIMER_BASE_HZ / SAMPLE_RATE, true);
  timerAlarmEnable(sampleTimer);
#endif
}

static void serviceDrdyPulse() {
  if (!DRDY_PULSE_MODE || !stats.drdyAsserted) return;
  if ((int32_t)(micros() - drdyReleaseAtUs) >= 0) {
    gpio_set_level((gpio_num_t)DRDY_PIN, drdyInactiveLevel());
    stats.drdyAsserted = false;
  }
}

// ----------------------------- SPI ------------------------------------

#if defined(SPI2_HOST)
static const spi_host_device_t SPI_SLAVE_HOST = SPI2_HOST;
#else
static const spi_host_device_t SPI_SLAVE_HOST = HSPI_HOST;
#endif

static void IRAM_ATTR onSpiPostTransaction(spi_slave_transaction_t *trans) {
  (void)trans;
  stats.framesSent++;
  if (!DRDY_PULSE_MODE) {
    gpio_set_level((gpio_num_t)DRDY_PIN, drdyInactiveLevel());
    stats.drdyAsserted = false;
  }
}

static void freeSpi() {
  if (spiStarted) {
    spi_slave_free(SPI_SLAVE_HOST);
    spiStarted = false;
  }
}

static bool queueSpiBuffer(uint8_t index) {
  memset(&spiTransactions[index], 0, sizeof(spiTransactions[index]));
  memcpy(txBuffers[index], currentFrame, currentFrameBytes);
  spiTransactions[index].length = (size_t)currentFrameBytes * 8;
  spiTransactions[index].tx_buffer = txBuffers[index];
  spiTransactions[index].rx_buffer = rxBuffers[index];
  esp_err_t err = spi_slave_queue_trans(SPI_SLAVE_HOST, &spiTransactions[index], 0);
  return err == ESP_OK;
}

static bool startSpi() {
  freeSpi();

  spi_bus_config_t busConfig = {};
  busConfig.mosi_io_num = PIN_SPI_MOSI;
  busConfig.miso_io_num = PIN_SPI_MISO;
  busConfig.sclk_io_num = PIN_SPI_SCLK;
  busConfig.quadwp_io_num = -1;
  busConfig.quadhd_io_num = -1;
  busConfig.max_transfer_sz = MAX_FRAME_BYTES;

  spi_slave_interface_config_t slaveConfig = {};
  slaveConfig.mode = SPI_MODE;
  slaveConfig.spics_io_num = PIN_SPI_CS;
  slaveConfig.queue_size = SPI_QUEUE_DEPTH;
  slaveConfig.flags = 0;
  slaveConfig.post_trans_cb = onSpiPostTransaction;

  esp_err_t err = spi_slave_initialize(SPI_SLAVE_HOST, &busConfig, &slaveConfig, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    Serial.print(F("SPI init failed: "));
    Serial.println(esp_err_to_name(err));
    return false;
  }

  buildFrame();
  for (uint8_t i = 0; i < SPI_QUEUE_DEPTH; i++) {
    if (!queueSpiBuffer(i)) {
      Serial.println(F("SPI queue failed"));
      freeSpi();
      return false;
    }
  }

  spiStarted = true;
  return true;
}

static void serviceSpi() {
  if (!spiStarted) return;

  spi_slave_transaction_t *done = nullptr;
  while (spi_slave_get_trans_result(SPI_SLAVE_HOST, &done, 0) == ESP_OK && done) {
    stats.spiFrames++;
    for (uint8_t i = 0; i < SPI_QUEUE_DEPTH; i++) {
      if (done == &spiTransactions[i]) {
        queueSpiBuffer(i);
        break;
      }
    }
    done = nullptr;
  }
}

// -------------------------- RUNTIME CONTROL ---------------------------

static void initializeChannels(SignalMode mode) {
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    channels[ch].mode = mode;
    channels[ch].constantValue = DEFAULT_CONSTANT * (ch + 1);
    channels[ch].sineFreqHz = DEFAULT_SINE_FREQ_HZ * (1.0f + 0.05f * ch);
    channels[ch].amplitude = DEFAULT_AMPLITUDE;
    channels[ch].offset = DEFAULT_OFFSET;
    channels[ch].phase = (uint32_t)ch * 0x11111111u;
    channels[ch].counter = 0;
  }
  updateChannelPhaseSteps();
}

static void startStreaming() {
  stats.running = true;
  stats.sampleDue = false;
  digitalWrite(DRDY_PIN, drdyInactiveLevel());
  Serial.println(F("Streaming START"));
}

static void stopStreaming() {
  stats.running = false;
  stats.sampleDue = false;
  digitalWrite(DRDY_PIN, drdyInactiveLevel());
  Serial.println(F("Streaming STOP"));
}

static bool setBits(uint8_t bits) {
  if (bits != 16 && bits != 24 && bits != 32) return false;
  WORD_LENGTH = bits;
  currentFrameBytes = frameBytes();
  return startSpi();
}

static bool setSampleRate(uint32_t rate) {
  if (rate < 1000 || rate > 32000) return false;
  SAMPLE_RATE = rate;
  updateChannelPhaseSteps();
  configureSampleTimer();
  return true;
}

static bool setSpiMode(uint8_t mode) {
  if (mode > 3) return false;
  SPI_MODE = mode;
  return startSpi();
}

static bool setSignal(SignalMode mode) {
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    channels[ch].mode = mode;
  }
  return true;
}

// ----------------------------- SERIAL ---------------------------------

static void printConfig() {
  Serial.println(F(""));
  Serial.println(F("ADS131M08 ESP32 SPI slave emulator"));
  Serial.print(F("WORD_LENGTH=")); Serial.println(WORD_LENGTH);
  Serial.print(F("SAMPLE_RATE=")); Serial.println(SAMPLE_RATE);
  Serial.print(F("SPI_MAX_HZ=")); Serial.println(SPI_CLOCK_MAX_HZ);
  Serial.print(F("SPI_MODE=")); Serial.println(SPI_MODE);
  Serial.print(F("CHANNELS=")); Serial.println(NUM_CHANNELS);
  Serial.print(F("ENABLE_CRC=")); Serial.println(ENABLE_CRC ? F("true") : F("false"));
  Serial.print(F("FRAME_BYTES=")); Serial.println(frameBytes());
  Serial.print(F("DRDY_PIN=")); Serial.println(DRDY_PIN);
  Serial.print(F("DRDY_MODE=")); Serial.println(DRDY_PULSE_MODE ? F("PULSE") : F("LEVEL"));
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    Serial.print(F("CH")); Serial.print(ch);
    Serial.print(F("=")); Serial.print(signalName(channels[ch].mode));
    Serial.print(F(" freq=")); Serial.print(channels[ch].sineFreqHz, 2);
    Serial.print(F(" amp=")); Serial.print(channels[ch].amplitude, 3);
    Serial.print(F(" offset=")); Serial.print(channels[ch].offset);
    Serial.print(F(" const=")); Serial.println(channels[ch].constantValue);
  }
}

static void printStats() {
  Serial.print(F("FPS=")); Serial.print(stats.fps, 1);
  Serial.print(F(" SAMPLE_FPS=")); Serial.print(stats.sampleFps, 1);
  Serial.print(F(" frames_sent=")); Serial.print((uint32_t)stats.framesSent);
  Serial.print(F(" spi_done=")); Serial.print((uint32_t)stats.spiFrames);
  Serial.print(F(" sample_ticks=")); Serial.print((uint32_t)stats.sampleTicks);
  Serial.print(F(" overruns=")); Serial.print((uint32_t)stats.overruns);
  Serial.print(F(" cpu_build_pct=")); Serial.print(stats.cpuPct, 2);
  Serial.print(F(" running=")); Serial.println(stats.running ? F("true") : F("false"));
}

static void updateStats() {
  const uint32_t now = millis();
  if (now - stats.lastStatsMs < 1000) return;

  const uint32_t elapsed = now - stats.lastStatsMs;
  const uint32_t frames = stats.framesSent;
  const uint32_t ticks = stats.sampleTicks;
  stats.fps = ((frames - stats.lastFramesSent) * 1000.0f) / (float)elapsed;
  stats.sampleFps = ((ticks - stats.lastSampleTicks) * 1000.0f) / (float)elapsed;
  stats.cpuPct = ((float)stats.buildMicrosAccum / ((float)elapsed * 1000.0f)) * 100.0f;

  stats.lastFramesSent = frames;
  stats.lastSampleTicks = ticks;
  stats.lastStatsMs = now;
  stats.buildMicrosAccum = 0;

  printStats();
}

static void handleCommand(char *line) {
  char *cmd = strtok(line, " \t\r\n");
  if (!cmd) return;

  if (!strcasecmp(cmd, "BITS")) {
    char *arg = strtok(nullptr, " \t\r\n");
    if (arg && setBits((uint8_t)atoi(arg))) Serial.println(F("OK"));
    else Serial.println(F("ERR BITS expects 16, 24, or 32"));
  } else if (!strcasecmp(cmd, "RATE")) {
    char *arg = strtok(nullptr, " \t\r\n");
    if (arg && setSampleRate((uint32_t)atol(arg))) Serial.println(F("OK"));
    else Serial.println(F("ERR RATE expects 1000..32000"));
  } else if (!strcasecmp(cmd, "SPI")) {
    char *arg = strtok(nullptr, " \t\r\n");
    if (arg) {
      SPI_CLOCK_MAX_HZ = (uint32_t)atol(arg);
      Serial.println(F("OK"));
    } else {
      Serial.println(F("ERR SPI expects max Hz"));
    }
  } else if (!strcasecmp(cmd, "SPIMODE")) {
    char *arg = strtok(nullptr, " \t\r\n");
    if (arg && setSpiMode((uint8_t)atoi(arg))) Serial.println(F("OK"));
    else Serial.println(F("ERR SPIMODE expects 0..3"));
  } else if (!strcasecmp(cmd, "CRC")) {
    char *arg = strtok(nullptr, " \t\r\n");
    if (arg && (!strcasecmp(arg, "ON") || !strcasecmp(arg, "TRUE") || !strcmp(arg, "1"))) {
      ENABLE_CRC = true;
      Serial.println(startSpi() ? F("OK") : F("ERR SPI restart failed"));
    } else if (arg && (!strcasecmp(arg, "OFF") || !strcasecmp(arg, "FALSE") || !strcmp(arg, "0"))) {
      ENABLE_CRC = false;
      Serial.println(startSpi() ? F("OK") : F("ERR SPI restart failed"));
    } else {
      Serial.println(F("ERR CRC expects ON or OFF"));
    }
  } else if (!strcasecmp(cmd, "MODE")) {
    char *arg = strtok(nullptr, " \t\r\n");
    SignalMode mode;
    if (arg && parseSignalMode(arg, mode) && setSignal(mode)) Serial.println(F("OK"));
    else Serial.println(F("ERR MODE expects CONSTANT, COUNTER, SINE, TRIANGLE, or RANDOM"));
  } else if (!strcasecmp(cmd, "CH")) {
    char *chArg = strtok(nullptr, " \t\r\n");
    char *modeArg = strtok(nullptr, " \t\r\n");
    char *valueArg = strtok(nullptr, " \t\r\n");
    SignalMode mode;
    int ch = chArg ? atoi(chArg) : -1;
    if (ch >= 0 && ch < NUM_CHANNELS && modeArg && parseSignalMode(modeArg, mode)) {
      channels[ch].mode = mode;
      if (valueArg) {
        if (mode == SIGNAL_SINE || mode == SIGNAL_TRIANGLE) channels[ch].sineFreqHz = atof(valueArg);
        else channels[ch].constantValue = atol(valueArg);
      }
      updateChannelPhaseSteps();
      Serial.println(F("OK"));
    } else {
      Serial.println(F("ERR CH expects: CH <0..7> <MODE> [value]"));
    }
  } else if (!strcasecmp(cmd, "START")) {
    startStreaming();
  } else if (!strcasecmp(cmd, "STOP")) {
    stopStreaming();
  } else if (!strcasecmp(cmd, "CONFIG")) {
    printConfig();
  } else if (!strcasecmp(cmd, "STATS")) {
    printStats();
  } else {
    Serial.println(F("ERR unknown command"));
  }
}

static void serviceSerial() {
  while (Serial.available()) {
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

// ----------------------------- ARDUINO --------------------------------

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 1200) {
    yield();
  }

  pinMode(DRDY_PIN, OUTPUT);
  digitalWrite(DRDY_PIN, drdyInactiveLevel());

  initializeChannels(SIGNAL_SINE);
  currentFrameBytes = frameBytes();
  stats.lastStatsMs = millis();

  configureSampleTimer();
  if (!startSpi()) {
    Serial.println(F("SPI is not running. Check selected pins and ESP32 target."));
  }

  printConfig();
  startStreaming();
}

void loop() {
  serviceSerial();
  serviceDrdyPulse();

  if (stats.sampleDue) {
    portENTER_CRITICAL(&timerMux);
    stats.sampleDue = false;
    portEXIT_CRITICAL(&timerMux);
    buildFrame();
  }

  serviceSpi();
  updateStats();
}
