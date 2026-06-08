#include <SDL2/SDL.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kMaxChannels = 8;
constexpr int kDisplayBuffers = 2;
constexpr size_t kRingSize = 1u << 16;
constexpr double kAdc24FullScale = 8388607.0;
constexpr std::array<const char *, kMaxChannels> kNames = {
    "VA", "VB", "VC", "VAN", "IA", "IB", "IC", "IN"};

struct Color {
  uint8_t r, g, b, a;
};

constexpr std::array<Color, kMaxChannels> kTraceColors = {{
    {255, 220, 30, 255},   // VA yellow
    {20, 220, 255, 255},   // VB cyan
    {255, 70, 230, 255},   // VC magenta
    {80, 255, 120, 255},   // VAN green
    {255, 150, 40, 255},   // IA orange
    {100, 170, 255, 255},  // IB blue
    {210, 110, 255, 255},  // IC violet
    {180, 180, 180, 255},  // IN gray
}};

struct Args {
  std::string spi_dev = "/dev/spidev0.0";
  std::string serial_dev;
  uint32_t spi_hz = 5000000;
  uint8_t spi_mode = 0;
  int word_bits = 24;
  int channels = 8;
  int sample_rate = 4000;
  int drdy_gpio = 4;
  bool crc = false;
  bool fullscreen = true;
  bool phosphor = false;
};

struct Sample {
  uint64_t seq = 0;
  uint32_t status = 0;
  std::array<float, kMaxChannels> ch{};
};

struct ScopeState {
  std::atomic<bool> running{true};
  std::atomic<bool> capture{true};
  std::atomic<uint64_t> write_seq{0};
  std::atomic<uint64_t> frames{0};
  std::atomic<uint64_t> errors{0};
  std::atomic<uint64_t> overruns{0};
  std::atomic<int> front_display{0};
  std::atomic<int> trigger_channel{0};
  std::atomic<bool> trigger_rising{true};
  std::atomic<bool> trigger_auto{true};
  std::atomic<float> trigger_level{0.0f};
  std::atomic<float> volts_per_div{0.25f};
  std::atomic<float> time_per_div{0.010f};  // 10 ms/div = 6 cycles at 60 Hz over 10 div
  std::atomic<bool> show_channel[kMaxChannels];
};

struct DisplayFrame {
  size_t count = 0;
  uint64_t end_seq = 0;
  bool triggered = false;
  std::vector<Sample> samples;
};

int32_t signExtend(uint32_t value, int bits) {
  const uint32_t sign = 1u << (bits - 1);
  const uint32_t mask = bits == 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u);
  value &= mask;
  return (value & sign) ? static_cast<int32_t>(value | ~mask) : static_cast<int32_t>(value);
}

uint16_t crc16Ccitt(const std::vector<uint8_t> &data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

uint32_t readWord(const std::vector<uint8_t> &frame, size_t offset, int word_bits) {
  uint32_t value = 0;
  const size_t bytes = static_cast<size_t>(word_bits / 8);
  for (size_t i = 0; i < bytes; ++i) value = (value << 8) | frame[offset + i];
  return value;
}

int32_t decodeSignedWord(const std::vector<uint8_t> &frame, size_t offset, int word_bits) {
  uint32_t raw = readWord(frame, offset, word_bits);
  if (word_bits == 16) return signExtend(raw, 16) << 8;
  if (word_bits == 24) return signExtend(raw, 24);
  return signExtend(raw, 32) / 256;
}

bool decodeFrame(const std::vector<uint8_t> &rx, const Args &args, Sample &out) {
  const size_t word_bytes = static_cast<size_t>(args.word_bits / 8);
  const size_t expected = static_cast<size_t>(1 + args.channels + (args.crc ? 1 : 0)) * word_bytes;
  if (rx.size() != expected) return false;
  if (args.crc) {
    const size_t payload_len = expected - word_bytes;
    const uint16_t got = static_cast<uint16_t>(readWord(rx, payload_len, args.word_bits) & 0xFFFF);
    if (crc16Ccitt(rx, payload_len) != got) return false;
  }
  out.status = readWord(rx, 0, args.word_bits);
  if (args.word_bits == 24 && (out.status & 0xFF0000u) != 0x050000u) return false;
  if (args.word_bits == 16 && (out.status & 0xFF00u) != 0x0500u) return false;
  if (args.word_bits == 32 && (out.status & 0xFF000000u) != 0x05000000u) return false;
  for (int ch = 0; ch < args.channels; ++ch) {
    int32_t raw = decodeSignedWord(rx, static_cast<size_t>(ch + 1) * word_bytes, args.word_bits);
    out.ch[ch] = std::clamp(static_cast<float>(raw / kAdc24FullScale), -1.0f, 1.0f);
  }
  return true;
}

void rejectSpikes(Sample &sample, const Sample &last, int channels) {
  if (last.seq == 0) return;
  for (int ch = 0; ch < channels; ++ch) {
    const float delta = std::fabs(sample.ch[ch] - last.ch[ch]);
    if (delta > 0.35f) {
      sample.ch[ch] = last.ch[ch];
    }
  }
}

std::string autoSerialPort() {
  for (const char *prefix : {"/dev/ttyUSB", "/dev/ttyACM"}) {
    for (int i = 0; i < 8; ++i) {
      std::string path = std::string(prefix) + std::to_string(i);
      if (std::filesystem::exists(path)) return path;
    }
  }
  return {};
}

speed_t baudToTermios(int baud) {
  switch (baud) {
    case 57600: return B57600;
    case 115200: return B115200;
    case 921600: return B921600;
    default: return B115200;
  }
}

int openSerial(const std::string &path, int baud = 115200) {
  if (path.empty()) return -1;
  int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) return -1;
  termios tty{};
  if (tcgetattr(fd, &tty) != 0) return fd;
  cfmakeraw(&tty);
  cfsetispeed(&tty, baudToTermios(baud));
  cfsetospeed(&tty, baudToTermios(baud));
  tty.c_cflag |= CLOCAL | CREAD;
  tty.c_cflag &= ~CRTSCTS;
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~PARENB;
  tty.c_cflag &= ~CSIZE;
  tty.c_cflag |= CS8;
  tcsetattr(fd, TCSANOW, &tty);
  return fd;
}

void serialCommand(int fd, const std::string &cmd) {
  if (fd < 0) return;
  std::string line = cmd + "\n";
  ::write(fd, line.data(), line.size());
  tcdrain(fd);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

void configureEsp32(int fd, const Args &args) {
  serialCommand(fd, "BITS " + std::to_string(args.word_bits));
  serialCommand(fd, "RATE " + std::to_string(args.sample_rate));
  serialCommand(fd, "SPI " + std::to_string(args.spi_hz));
  serialCommand(fd, "SPIMODE " + std::to_string(args.spi_mode));
  serialCommand(fd, std::string("CRC ") + (args.crc ? "ON" : "OFF"));
  serialCommand(fd, "MODE SINE");
  serialCommand(fd, "START");
}

int openSpi(const Args &args) {
  int fd = ::open(args.spi_dev.c_str(), O_RDWR);
  if (fd < 0) return -1;
  uint8_t mode = args.spi_mode;
  uint8_t bits = 8;
  uint32_t hz = args.spi_hz;
  if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) return -1;
  if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) return -1;
  if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &hz) < 0) return -1;
  return fd;
}

bool spiTransfer(int fd, uint32_t hz, const std::vector<uint8_t> &tx, std::vector<uint8_t> &rx) {
  rx.assign(tx.size(), 0);
  spi_ioc_transfer tr{};
  tr.tx_buf = reinterpret_cast<uintptr_t>(tx.data());
  tr.rx_buf = reinterpret_cast<uintptr_t>(rx.data());
  tr.len = static_cast<uint32_t>(tx.size());
  tr.speed_hz = hz;
  tr.bits_per_word = 8;
  return ioctl(fd, SPI_IOC_MESSAGE(1), &tr) >= 0;
}

bool writeTextFile(const std::string &path, const std::string &value) {
  int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
  if (fd < 0) return false;
  bool ok = ::write(fd, value.data(), value.size()) == static_cast<ssize_t>(value.size());
  ::close(fd);
  return ok;
}

int openDrdyGpio(int gpio) {
  if (gpio < 0) return -1;
  const std::string base = "/sys/class/gpio/gpio" + std::to_string(gpio);
  if (!std::filesystem::exists(base + "/value")) {
    writeTextFile("/sys/class/gpio/export", std::to_string(gpio));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  writeTextFile(base + "/direction", "in");
  writeTextFile(base + "/edge", "falling");
  int fd = ::open((base + "/value").c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) return -1;
  char c;
  ::lseek(fd, 0, SEEK_SET);
  ::read(fd, &c, 1);
  return fd;
}

bool waitForDrdy(int fd, int timeout_ms) {
  if (fd < 0) return false;
  pollfd pfd{};
  pfd.fd = fd;
  pfd.events = POLLPRI | POLLERR;
  int rc = ::poll(&pfd, 1, timeout_ms);
  if (rc <= 0) return false;
  char c;
  ::lseek(fd, 0, SEEK_SET);
  ::read(fd, &c, 1);
  return true;
}

void pinThreadToCpu(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

void acquisitionThread(const Args args, ScopeState *state, std::vector<Sample> *ring) {
  pinThreadToCpu(1);
  int spi = openSpi(args);
  if (spi < 0) {
    state->running = false;
    return;
  }
  const size_t frame_bytes =
      static_cast<size_t>(1 + args.channels + (args.crc ? 1 : 0)) * (args.word_bits / 8);
  std::vector<uint8_t> tx(frame_bytes, 0), rx(frame_bytes, 0);
  int drdy = openDrdyGpio(args.drdy_gpio);
  Sample last_good;
  bool have_last_good = false;
  auto next = std::chrono::steady_clock::now();

  while (state->running.load(std::memory_order_relaxed)) {
    if (!state->capture.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      next = std::chrono::steady_clock::now();
      continue;
    }

    if (drdy >= 0) {
      if (!waitForDrdy(drdy, 100)) {
        state->overruns.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
    }

    Sample sample;
    if (spiTransfer(spi, args.spi_hz, tx, rx) && decodeFrame(rx, args, sample)) {
      if (have_last_good) rejectSpikes(sample, last_good, args.channels);
      uint64_t seq = state->write_seq.fetch_add(1, std::memory_order_acq_rel);
      sample.seq = seq;
      (*ring)[seq & (kRingSize - 1)] = sample;
      last_good = sample;
      have_last_good = true;
      state->frames.fetch_add(1, std::memory_order_relaxed);
    } else {
      state->errors.fetch_add(1, std::memory_order_relaxed);
    }

    if (drdy < 0) {
      next += std::chrono::microseconds(1000000 / std::max(1, args.sample_rate));
      std::this_thread::sleep_until(next);
      if (std::chrono::steady_clock::now() > next + std::chrono::milliseconds(50)) {
        state->overruns.fetch_add(1, std::memory_order_relaxed);
        next = std::chrono::steady_clock::now();
      }
    }
  }
  if (drdy >= 0) ::close(drdy);
  ::close(spi);
}

size_t samplesPerScreen(const Args &args, const ScopeState &state) {
  double total_time = static_cast<double>(state.time_per_div.load()) * 10.0;
  double samples = total_time * static_cast<double>(args.sample_rate);
  return static_cast<size_t>(std::clamp(samples, 64.0, static_cast<double>(kRingSize / 2)));
}

uint64_t findTriggerStart(const Args &args, ScopeState *state, const std::vector<Sample> &ring,
                          uint64_t latest, size_t count, bool *triggered) {
  const int ch = std::clamp(state->trigger_channel.load(), 0, args.channels - 1);
  const float level = state->trigger_level.load();
  const bool rising = state->trigger_rising.load();
  *triggered = false;
  if (latest < 4) return latest > count ? latest - count : 0;

  const uint64_t scan_begin = latest > std::min<uint64_t>(count * 2, kRingSize - 2)
                                  ? latest - std::min<uint64_t>(count * 2, kRingSize - 2)
                                  : 1;
  for (uint64_t seq = latest - 1; seq > scan_begin; --seq) {
    const Sample &a = ring[(seq - 1) & (kRingSize - 1)];
    const Sample &b = ring[seq & (kRingSize - 1)];
    bool crossed = rising ? (a.ch[ch] < level && b.ch[ch] >= level)
                          : (a.ch[ch] > level && b.ch[ch] <= level);
    if (crossed) {
      *triggered = true;
      uint64_t pre = count / 4;
      return seq > pre ? seq - pre : 0;
    }
  }
  return latest > count ? latest - count : 0;
}

void processingThread(const Args args, ScopeState *state, const std::vector<Sample> *ring,
                      std::array<DisplayFrame, kDisplayBuffers> *display) {
  pinThreadToCpu(2);
  while (state->running.load(std::memory_order_relaxed)) {
    const uint64_t latest = state->write_seq.load(std::memory_order_acquire);
    const size_t count = samplesPerScreen(args, *state);
    if (latest > count + 4) {
      bool triggered = false;
      uint64_t start = findTriggerStart(args, state, *ring, latest, count, &triggered);
      if (triggered || state->trigger_auto.load()) {
        int back = 1 - state->front_display.load(std::memory_order_acquire);
        DisplayFrame &dst = (*display)[back];
        dst.count = count;
        dst.end_seq = start + count;
        dst.triggered = triggered;
        for (size_t i = 0; i < count; ++i) {
          dst.samples[i] = (*ring)[(start + i) & (kRingSize - 1)];
        }
        state->front_display.store(back, std::memory_order_release);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

void setColor(SDL_Renderer *r, Color c) {
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

void line(SDL_Renderer *r, float x1, float y1, float x2, float y2) {
  SDL_RenderDrawLineF(r, x1, y1, x2, y2);
}

std::array<uint8_t, 7> glyphRows(char ch) {
  switch (ch) {
    case 'A': return {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    case 'B': return {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    case 'C': return {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    case 'D': return {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
    case 'E': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    case 'F': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    case 'G': return {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E};
    case 'H': return {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    case 'I': return {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
    case 'J': return {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C};
    case 'K': return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    case 'M': return {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    case 'N': return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    case 'O': return {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    case 'P': return {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    case 'Q': return {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
    case 'R': return {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    case 'S': return {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    case 'T': return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    case 'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    case 'V': return {0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04};
    case 'W': return {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11};
    case 'X': return {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    case 'Y': return {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    case 'Z': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
    case '0': return {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    case '1': return {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    case '2': return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    case '3': return {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
    case '4': return {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    case '5': return {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
    case '6': return {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    case '7': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    case '8': return {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    case '9': return {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E};
    case '.': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
    case '/': return {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
    case '-': return {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    case '+': return {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
    case ':': return {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00};
    case '\'': return {0x0C, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00};
    default: return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  }
}

void drawText(SDL_Renderer *r, int x, int y, const std::string &text, Color color, int scale = 2) {
  setColor(r, color);
  int cx = x;
  for (char ch : text) {
    if (ch == ' ') {
      cx += 4 * scale;
      continue;
    }
    auto rows = glyphRows(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if (rows[row] & (1u << (4 - col))) {
          SDL_Rect rect{cx + col * scale, y + row * scale, scale, scale};
          SDL_RenderFillRect(r, &rect);
        }
      }
    }
    cx += 7 * scale;
  }
}

void drawGrid(SDL_Renderer *r, SDL_Rect area) {
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  for (int x = 0; x <= 50; ++x) {
    float px = area.x + area.w * (x / 50.0f);
    setColor(r, x % 5 == 0 ? Color{55, 65, 70, 180} : Color{28, 34, 38, 150});
    line(r, px, area.y, px, area.y + area.h);
  }
  for (int y = 0; y <= 40; ++y) {
    float py = area.y + area.h * (y / 40.0f);
    setColor(r, y % 5 == 0 ? Color{55, 65, 70, 180} : Color{28, 34, 38, 150});
    line(r, area.x, py, area.x + area.w, py);
  }
  setColor(r, {95, 110, 115, 230});
  line(r, area.x, area.y + area.h / 2.0f, area.x + area.w, area.y + area.h / 2.0f);
  line(r, area.x + area.w / 2.0f, area.y, area.x + area.w / 2.0f, area.y + area.h);
}

void drawTrace(SDL_Renderer *r, const DisplayFrame &frame, int channel, SDL_Rect area, float vdiv) {
  if (frame.count < 2) return;
  setColor(r, kTraceColors[channel]);
  const float center = area.y + area.h * 0.5f;
  const float scale = (area.h / 8.0f) / std::max(0.02f, vdiv);
  const float step = static_cast<float>(area.w) / static_cast<float>(frame.count - 1);
  float last_x = area.x;
  float last_y = center - frame.samples[0].ch[channel] * scale;
  for (size_t i = 1; i < frame.count; ++i) {
    float x = area.x + step * static_cast<float>(i);
    float y = center - frame.samples[i].ch[channel] * scale;
    line(r, last_x, last_y, x, y);
    last_x = x;
    last_y = y;
  }
}

void drawUi(SDL_Renderer *r, const Args &args, ScopeState *state, const DisplayFrame &frame,
            double render_fps) {
  int w = 0, h = 0;
  SDL_GetRendererOutputSize(r, &w, &h);
  if (args.phosphor) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    setColor(r, {0, 0, 0, 45});
    SDL_Rect full{0, 0, w, h};
    SDL_RenderFillRect(r, &full);
  } else {
    setColor(r, {0, 0, 0, 255});
    SDL_RenderClear(r);
  }

  SDL_Rect plot{70, 74, w - 110, h - 150};
  drawGrid(r, plot);

  for (int ch = 0; ch < args.channels; ++ch) {
    if (state->show_channel[ch].load()) drawTrace(r, frame, ch, plot, state->volts_per_div.load());
  }

  setColor(r, {12, 16, 18, 235});
  SDL_Rect top{0, 0, w, 54};
  SDL_RenderFillRect(r, &top);
  Color green{80, 255, 120, 255};
  Color amber{255, 210, 70, 255};
  Color gray{160, 170, 175, 255};

  std::string status = state->capture.load() ? "RUN" : "HOLD";
  std::string trig = frame.triggered ? "TRIG'D" : (state->trigger_auto.load() ? "AUTO" : "WAIT");
  char line1[256];
  std::snprintf(line1, sizeof(line1), "%s   %s   Fs %dS/s   %.3f ms/div   %.2f V/div   FPS %.0f",
                status.c_str(), trig.c_str(), args.sample_rate,
                state->time_per_div.load() * 1000.0f, state->volts_per_div.load(), render_fps);
  drawText(r, 18, 14, line1, state->capture.load() ? green : amber, 2);

  for (int i = 0; i < args.channels; ++i) {
    int x = 82 + i * 138;
    int y = h - 52;
    setColor(r, kTraceColors[i]);
    SDL_Rect sw{x, y, 18, 18};
    SDL_RenderFillRect(r, &sw);
    drawText(r, x + 28, y,
             std::string(kNames[i]) + (state->show_channel[i].load() ? " ON" : " OFF"),
             state->show_channel[i].load() ? kTraceColors[i] : gray, 2);
  }

  float trig_y = plot.y + plot.h * 0.5f - state->trigger_level.load() *
                                           ((plot.h / 8.0f) / state->volts_per_div.load());
  setColor(r, {255, 120, 40, 220});
  line(r, plot.x - 12, trig_y, plot.x + plot.w + 12, trig_y);
}

void handleEvent(const SDL_Event &event, ScopeState *state, const Args &args) {
  if (event.type != SDL_KEYDOWN) return;
  SDL_Keycode key = event.key.keysym.sym;
  if (key == SDLK_ESCAPE || key == SDLK_q) state->running = false;
  if (key == SDLK_SPACE) state->capture = !state->capture.load();
  if (key == SDLK_t) state->trigger_auto = !state->trigger_auto.load();
  if (key == SDLK_e) state->trigger_rising = !state->trigger_rising.load();
  if (key == SDLK_LEFTBRACKET) state->trigger_level = state->trigger_level.load() - 0.05f;
  if (key == SDLK_RIGHTBRACKET) state->trigger_level = state->trigger_level.load() + 0.05f;
  if (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS) {
    state->time_per_div = state->time_per_div.load() * 0.8f;
  }
  if (key == SDLK_MINUS) state->time_per_div = state->time_per_div.load() * 1.25f;
  if (key == SDLK_UP) state->volts_per_div = state->volts_per_div.load() * 0.8f;
  if (key == SDLK_DOWN) state->volts_per_div = state->volts_per_div.load() * 1.25f;
  if (key == SDLK_TAB) state->trigger_channel = (state->trigger_channel.load() + 1) % args.channels;
  if (key >= SDLK_1 && key <= SDLK_8) {
    int ch = static_cast<int>(key - SDLK_1);
    if (ch < args.channels) state->show_channel[ch] = !state->show_channel[ch].load();
  }
}

void renderThread(const Args args, ScopeState *state,
                  const std::array<DisplayFrame, kDisplayBuffers> *display) {
  pinThreadToCpu(3);
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    state->running = false;
    return;
  }
  uint32_t flags = SDL_WINDOW_SHOWN;
  if (args.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  SDL_Window *window = SDL_CreateWindow("ADS131M08 Instrument Scope", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, 1280, 720, flags);
  SDL_Renderer *renderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!window || !renderer) {
    state->running = false;
    SDL_Quit();
    return;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

  auto last = std::chrono::steady_clock::now();
  int frames = 0;
  double render_fps = 0.0;
  while (state->running.load()) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) state->running = false;
      handleEvent(event, state, args);
    }
    int front = state->front_display.load(std::memory_order_acquire);
    drawUi(renderer, args, state, (*display)[front], render_fps);
    SDL_RenderPresent(renderer);

    frames++;
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - last).count();
    if (elapsed >= 1.0) {
      render_fps = frames / elapsed;
      frames = 0;
      last = now;
    }
  }
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

void usage(const char *argv0) {
  std::cerr << "Usage: " << argv0
            << " [--spi-dev /dev/spidev0.0] [--serial /dev/ttyUSB0]"
            << " [--spi-hz 10000000] [--spi-mode 0] [--bits 24]"
            << " [--rate 4000] [--channels 8] [--drdy-gpio 4]"
            << " [--no-drdy] [--windowed] [--phosphor]\n";
}

bool parseArgs(int argc, char **argv, Args &args) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto need = [&](const char *name) -> const char * {
      if (i + 1 >= argc) {
        std::cerr << name << " expects a value\n";
        return nullptr;
      }
      return argv[++i];
    };
    if (a == "--spi-dev") {
      const char *v = need("--spi-dev");
      if (!v) return false;
      args.spi_dev = v;
    } else if (a == "--serial") {
      const char *v = need("--serial");
      if (!v) return false;
      args.serial_dev = v;
    } else if (a == "--spi-hz") {
      const char *v = need("--spi-hz");
      if (!v) return false;
      args.spi_hz = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
    } else if (a == "--spi-mode") {
      const char *v = need("--spi-mode");
      if (!v) return false;
      args.spi_mode = static_cast<uint8_t>(std::strtoul(v, nullptr, 10));
    } else if (a == "--bits") {
      const char *v = need("--bits");
      if (!v) return false;
      args.word_bits = std::atoi(v);
    } else if (a == "--rate") {
      const char *v = need("--rate");
      if (!v) return false;
      args.sample_rate = std::atoi(v);
    } else if (a == "--channels") {
      const char *v = need("--channels");
      if (!v) return false;
      args.channels = std::clamp(std::atoi(v), 1, kMaxChannels);
    } else if (a == "--drdy-gpio") {
      const char *v = need("--drdy-gpio");
      if (!v) return false;
      args.drdy_gpio = std::atoi(v);
    } else if (a == "--no-drdy") {
      args.drdy_gpio = -1;
    } else if (a == "--crc") {
      args.crc = true;
    } else if (a == "--windowed") {
      args.fullscreen = false;
    } else if (a == "--phosphor") {
      args.phosphor = true;
    } else if (a == "--no-phosphor") {
      args.phosphor = false;
    } else if (a == "--help" || a == "-h") {
      usage(argv[0]);
      return false;
    } else {
      std::cerr << "Unknown arg: " << a << "\n";
      return false;
    }
  }
  return args.word_bits == 16 || args.word_bits == 24 || args.word_bits == 32;
}

}  // namespace

int main(int argc, char **argv) {
  Args args;
  if (!parseArgs(argc, argv, args)) return 2;
  if (args.serial_dev.empty()) args.serial_dev = autoSerialPort();

  ScopeState state;
  for (int i = 0; i < kMaxChannels; ++i) state.show_channel[i] = i < args.channels;

  int serial = openSerial(args.serial_dev);
  configureEsp32(serial, args);

  std::vector<Sample> ring(kRingSize);
  std::array<DisplayFrame, kDisplayBuffers> display;
  for (auto &buf : display) {
    buf.samples.resize(kRingSize / 2);
  }

  std::thread acq(acquisitionThread, args, &state, &ring);
  std::thread proc(processingThread, args, &state, &ring, &display);
  std::thread rend(renderThread, args, &state, &display);

  rend.join();
  state.running = false;
  acq.join();
  proc.join();
  serialCommand(serial, "STOP");
  if (serial >= 0) ::close(serial);
  return 0;
}
