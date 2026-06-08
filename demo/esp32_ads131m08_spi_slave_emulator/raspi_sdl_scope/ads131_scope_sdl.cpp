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
constexpr size_t kMaxRenderPoints = 8192;
constexpr double kAdc24FullScale = 8388607.0;
constexpr float kLineHz = 60.0f;
constexpr float kVisibleCycles = 6.0f;
constexpr float kTraceUseHeight = 0.66f;
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
  uint32_t spi_hz = 2000000;
  uint8_t spi_mode = 0;
  int word_bits = 24;
  int channels = 8;
  int sample_rate = 4000;
  int drdy_gpio = 4;
  bool crc = false;
  bool fullscreen = true;
  bool phosphor = true;
  bool vsync = false;
};

struct Sample {
  uint64_t seq = 0;
  uint32_t status = 0;
  std::array<float, kMaxChannels> ch{};
};

enum ViewMode : int {
  VIEW_SPLIT = 0,
  VIEW_OVERLAY = 1,
  VIEW_STACKED = 2,
};

struct Measurements {
  float freq = 0.0f;
  float rms = 0.0f;
  float peak = 0.0f;
  float vpp = 0.0f;
  float phase = 0.0f;
  float thd = 0.0f;
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
  std::atomic<float> time_per_div{kVisibleCycles / kLineHz / 10.0f};
  std::atomic<float> horizontal_position{0.25f};
  std::atomic<int> selected_channel{0};
  std::atomic<int> view_mode{VIEW_SPLIT};
  std::atomic<bool> show_grid{true};
  std::atomic<bool> persistence{true};
  std::atomic<bool> panel_open{true};
  std::atomic<bool> show_channel[kMaxChannels];
};

struct DisplayFrame {
  size_t count = 0;
  uint64_t end_seq = 0;
  bool triggered = false;
  std::array<Measurements, kMaxChannels> measurements{};
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
  tty.c_cflag &= ~HUPCL;
  tcsetattr(fd, TCSANOW, &tty);
  int modem = TIOCM_DTR | TIOCM_RTS;
  ioctl(fd, TIOCMBIC, &modem);
  return fd;
}

void serialCommand(int fd, const std::string &cmd) {
  if (fd < 0) return;
  std::string line = cmd + "\n";
  ::write(fd, line.data(), line.size());
  tcdrain(fd);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

void waitForNextWallSecond() {
  using clock = std::chrono::system_clock;
  auto next = std::chrono::time_point_cast<std::chrono::seconds>(clock::now()) + std::chrono::seconds(1);
  std::this_thread::sleep_until(next - std::chrono::milliseconds(2));
  while (clock::now() < next) {
    std::this_thread::yield();
  }
}

void configureEsp32(int fd, const Args &args) {
  if (fd < 0) return;
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  tcflush(fd, TCIOFLUSH);
  serialCommand(fd, "STOP");
  for (int attempt = 0; attempt < 2; ++attempt) {
    serialCommand(fd, "BITS " + std::to_string(args.word_bits));
    serialCommand(fd, "RATE " + std::to_string(args.sample_rate));
    serialCommand(fd, "SPI " + std::to_string(args.spi_hz));
    serialCommand(fd, "SPIMODE " + std::to_string(args.spi_mode));
    serialCommand(fd, std::string("CRC ") + (args.crc ? "ON" : "OFF"));
    serialCommand(fd, "MODE SINE");
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
  }
  waitForNextWallSecond();
  serialCommand(fd, "SYNCSTART");
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

int resolveSysfsGpio(int bcm_gpio) {
  if (bcm_gpio < 0) return -1;
  if (std::filesystem::exists("/sys/class/gpio/gpio" + std::to_string(bcm_gpio) + "/value")) {
    return bcm_gpio;
  }

  int fallback = bcm_gpio;
  for (const auto &entry : std::filesystem::directory_iterator("/sys/class/gpio")) {
    const std::string name = entry.path().filename().string();
    if (name.rfind("gpiochip", 0) != 0) continue;

    const std::string base_path = entry.path().string() + "/base";
    const std::string label_path = entry.path().string() + "/label";
    FILE *base_file = std::fopen(base_path.c_str(), "r");
    if (!base_file) continue;
    int base = 0;
    if (std::fscanf(base_file, "%d", &base) != 1) {
      std::fclose(base_file);
      continue;
    }
    std::fclose(base_file);

    char label[128] = {};
    FILE *label_file = std::fopen(label_path.c_str(), "r");
    if (label_file) {
      std::fgets(label, sizeof(label), label_file);
      std::fclose(label_file);
    }
    std::string label_text(label);
    if (label_text.find("gpio") != std::string::npos ||
        label_text.find("pinctrl") != std::string::npos ||
        label_text.find("3f200000") != std::string::npos ||
        label_text.find("fe200000") != std::string::npos) {
      fallback = base + bcm_gpio;
      if (std::filesystem::exists("/sys/class/gpio/gpio" + std::to_string(fallback) + "/value")) {
        return fallback;
      }
    }
  }
  return fallback;
}

int openDrdyGpio(int gpio) {
  if (gpio < 0) return -1;
  const int sysfs_gpio = resolveSysfsGpio(gpio);
  const std::string base = "/sys/class/gpio/gpio" + std::to_string(sysfs_gpio);
  if (!std::filesystem::exists(base + "/value")) {
    writeTextFile("/sys/class/gpio/export", std::to_string(sysfs_gpio));
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

int readGpioValue(int fd) {
  if (fd < 0) return -1;
  char c;
  ::lseek(fd, 0, SEEK_SET);
  if (::read(fd, &c, 1) != 1) return -1;
  return c == '0' ? 0 : 1;
}

bool waitForDrdyActive(int fd, int timeout_ms) {
  if (fd < 0) return false;
  if (readGpioValue(fd) == 0) {
    return true;
  }
  pollfd pfd{};
  pfd.fd = fd;
  pfd.events = POLLPRI | POLLERR;
  int rc = ::poll(&pfd, 1, timeout_ms);
  if (rc <= 0) return false;
  return readGpioValue(fd) == 0;
}

bool waitForDrdyInactive(int fd, int timeout_us) {
  if (fd < 0) return false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(timeout_us);
  while (std::chrono::steady_clock::now() < deadline) {
    if (readGpioValue(fd) == 1) return true;
    std::this_thread::sleep_for(std::chrono::microseconds(20));
  }
  return readGpioValue(fd) == 1;
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
  std::cerr << (drdy >= 0 ? "DRDY sync enabled\n" : "DRDY unavailable, timed polling fallback\n");
  auto next = std::chrono::steady_clock::now();

  while (state->running.load(std::memory_order_relaxed)) {
    if (!state->capture.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      next = std::chrono::steady_clock::now();
      continue;
    }

    if (drdy >= 0) {
      if (!waitForDrdyActive(drdy, 100)) {
        state->overruns.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
    }

    Sample sample;
    if (spiTransfer(spi, args.spi_hz, tx, rx) && decodeFrame(rx, args, sample)) {
      uint64_t seq = state->write_seq.fetch_add(1, std::memory_order_acq_rel);
      sample.seq = seq;
      (*ring)[seq & (kRingSize - 1)] = sample;
      state->frames.fetch_add(1, std::memory_order_relaxed);
    } else {
      state->errors.fetch_add(1, std::memory_order_relaxed);
    }

    if (drdy >= 0) {
      waitForDrdyInactive(drdy, 2000);
    } else {
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

  const float pos = std::clamp(state->horizontal_position.load(), 0.08f, 0.85f);
  const uint64_t pre = std::clamp<uint64_t>(static_cast<uint64_t>(count * pos), 8, count - 8);
  const uint64_t post = count - pre;
  const uint64_t scan_from = latest > post ? latest - post : 1;
  const uint64_t scan_span = std::min<uint64_t>(count, kRingSize - 2);
  const uint64_t scan_begin = scan_from > scan_span ? scan_from - scan_span : 1;
  for (uint64_t seq = scan_from; seq > scan_begin; --seq) {
    const Sample &a = ring[(seq - 1) & (kRingSize - 1)];
    const Sample &b = ring[seq & (kRingSize - 1)];
    bool crossed = rising ? (a.ch[ch] < level && b.ch[ch] >= level)
                          : (a.ch[ch] > level && b.ch[ch] <= level);
    if (crossed) {
      *triggered = true;
      return seq > pre ? seq - pre : 0;
    }
  }
  return latest > count ? latest - count : 0;
}

Measurements measureChannel(const DisplayFrame &frame, int channel, int sample_rate) {
  Measurements m{};
  if (frame.count < 4) return m;

  float min_v = 1.0f, max_v = -1.0f;
  double sum_sq = 0.0;
  int rising_crossings = 0;
  size_t first_cross = 0, last_cross = 0;
  const float level = 0.0f;

  for (size_t i = 0; i < frame.count; ++i) {
    const float v = frame.samples[i].ch[channel];
    min_v = std::min(min_v, v);
    max_v = std::max(max_v, v);
    sum_sq += static_cast<double>(v) * static_cast<double>(v);
    if (i > 0) {
      const float a = frame.samples[i - 1].ch[channel];
      if (a < level && v >= level) {
        if (rising_crossings == 0) first_cross = i;
        last_cross = i;
        rising_crossings++;
      }
    }
  }

  m.peak = std::max(std::fabs(min_v), std::fabs(max_v));
  m.vpp = max_v - min_v;
  m.rms = std::sqrt(sum_sq / static_cast<double>(frame.count));
  if (rising_crossings >= 2 && last_cross > first_cross) {
    const double cycles = static_cast<double>(rising_crossings - 1);
    const double seconds = static_cast<double>(last_cross - first_cross) / std::max(1, sample_rate);
    m.freq = static_cast<float>(cycles / std::max(0.000001, seconds));
  }
  m.phase = 0.0f;
  m.thd = 0.0f;
  return m;
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
        for (int ch = 0; ch < args.channels; ++ch) {
          dst.measurements[ch] = measureChannel(dst, ch, args.sample_rate);
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

void thickLine(SDL_Renderer *r, float x1, float y1, float x2, float y2, int width) {
  if (width <= 1) {
    line(r, x1, y1, x2, y2);
    return;
  }
  const float dx = std::fabs(x2 - x1);
  const float dy = std::fabs(y2 - y1);
  const int half = width / 2;
  for (int o = -half; o <= half; ++o) {
    if (dx > dy) line(r, x1, y1 + o, x2, y2 + o);
    else line(r, x1 + o, y1, x2 + o, y2);
  }
}

Color scaled(Color c, float factor, uint8_t alpha) {
  c.r = static_cast<uint8_t>(std::clamp(c.r * factor, 0.0f, 255.0f));
  c.g = static_cast<uint8_t>(std::clamp(c.g * factor, 0.0f, 255.0f));
  c.b = static_cast<uint8_t>(std::clamp(c.b * factor, 0.0f, 255.0f));
  c.a = alpha;
  return c;
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

void drawText(SDL_Renderer *r, int x, int y, const char *text, Color color, int scale = 2) {
  setColor(r, color);
  int cx = x;
  for (const char *p = text; *p; ++p) {
    char ch = *p;
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

void fillRect(SDL_Renderer *r, SDL_Rect rect, Color color) {
  setColor(r, color);
  SDL_RenderFillRect(r, &rect);
}

void drawRect(SDL_Renderer *r, SDL_Rect rect, Color color) {
  setColor(r, color);
  SDL_RenderDrawRect(r, &rect);
}

struct RenderCache {
  SDL_Texture *grid = nullptr;
  SDL_Rect grid_area{0, 0, 0, 0};
  bool grid_valid = false;
};

void destroyCache(RenderCache *cache) {
  if (cache->grid) SDL_DestroyTexture(cache->grid);
  cache->grid = nullptr;
  cache->grid_valid = false;
}

void buildGridTexture(SDL_Renderer *r, RenderCache *cache, SDL_Rect area) {
  if (cache->grid_valid && cache->grid && cache->grid_area.x == area.x &&
      cache->grid_area.y == area.y && cache->grid_area.w == area.w &&
      cache->grid_area.h == area.h) {
    return;
  }

  destroyCache(cache);
  cache->grid_area = area;
  cache->grid = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                  std::max(1, area.w), std::max(1, area.h));
  if (!cache->grid) return;
  SDL_SetTextureBlendMode(cache->grid, SDL_BLENDMODE_BLEND);
  SDL_SetRenderTarget(r, cache->grid);
  fillRect(r, SDL_Rect{0, 0, area.w, area.h}, {0, 0, 0, 0});

  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  for (int x = 0; x <= 50; ++x) {
    float px = area.w * (x / 50.0f);
    const bool major = x % 5 == 0;
    const bool center = x == 25;
    setColor(r, center ? Color{100, 120, 126, 125}
                       : (major ? Color{82, 94, 100, 74} : Color{58, 68, 74, 24}));
    line(r, px, 0, px, area.h);
  }
  for (int y = 0; y <= 40; ++y) {
    float py = area.h * (y / 40.0f);
    const bool major = y % 5 == 0;
    const bool center = y == 20;
    setColor(r, center ? Color{100, 120, 126, 125}
                       : (major ? Color{82, 94, 100, 74} : Color{58, 68, 74, 24}));
    line(r, 0, py, area.w, py);
  }
  SDL_SetRenderTarget(r, nullptr);
  cache->grid_valid = true;
}

void drawGrid(SDL_Renderer *r, RenderCache *cache, SDL_Rect area, bool show_grid) {
  if (!show_grid) return;
  buildGridTexture(r, cache, area);
  if (cache->grid) SDL_RenderCopy(r, cache->grid, nullptr, &area);
}

SDL_Rect plotForChannel(SDL_Rect plot, int channel, int mode) {
  if (mode == VIEW_OVERLAY) return plot;
  if (mode == VIEW_SPLIT) {
    const int gap = 14;
    const int half = (plot.h - gap) / 2;
    if (channel < 4) return SDL_Rect{plot.x, plot.y, plot.w, half};
    return SDL_Rect{plot.x, plot.y + half + gap, plot.w, half};
  }
  const int gap = 4;
  const int lane = (plot.h - gap * 7) / 8;
  return SDL_Rect{plot.x, plot.y + channel * (lane + gap), plot.w, lane};
}

const char *viewModeName(int mode) {
  switch (mode) {
    case VIEW_OVERLAY: return "OVERLAY";
    case VIEW_STACKED: return "STACKED";
    default: return "SPLIT";
  }
}

float groupPeak(const DisplayFrame &frame, int channel, int mode, int channels) {
  float peak = 0.05f;
  int first = channel, last = channel;
  if (mode == VIEW_OVERLAY) {
    first = 0;
    last = channels - 1;
  } else if (mode == VIEW_SPLIT) {
    first = channel < 4 ? 0 : 4;
    last = channel < 4 ? 3 : channels - 1;
  }
  for (int ch = first; ch <= last; ++ch) {
    for (size_t i = 0; i < frame.count; ++i) {
      peak = std::max(peak, std::fabs(frame.samples[i].ch[ch]));
    }
  }
  return peak;
}

void drawPolyline(SDL_Renderer *r, SDL_FPoint *points, int count, int width) {
  if (count < 2) return;
  if (width <= 1) {
    SDL_RenderDrawLinesF(r, points, count);
    return;
  }
  for (int o = -width / 2; o <= width / 2; ++o) {
    for (int i = 0; i < count; ++i) points[i].y += static_cast<float>(o);
    SDL_RenderDrawLinesF(r, points, count);
    for (int i = 0; i < count; ++i) points[i].y -= static_cast<float>(o);
  }
}

void drawTrace(SDL_Renderer *r, const DisplayFrame &frame, int channel, SDL_Rect area,
               float vdiv, float peak, Color color, int width) {
  if (frame.count < 2) return;
  static thread_local std::array<SDL_FPoint, kMaxRenderPoints> points;
  const size_t point_count = std::min(frame.count, kMaxRenderPoints);
  const float center = area.y + area.h * 0.5f;
  const float manual_scale = (area.h / 8.0f) / std::max(0.02f, vdiv);
  const float auto_scale = (area.h * kTraceUseHeight * 0.5f) / std::max(0.05f, peak);
  const float scale = std::min(manual_scale, auto_scale);
  const float step = static_cast<float>(area.w) / static_cast<float>(point_count - 1);
  setColor(r, color);
  for (size_t i = 0; i < point_count; ++i) {
    points[i].x = area.x + step * static_cast<float>(i);
    points[i].y = center - frame.samples[i].ch[channel] * scale;
  }
  drawPolyline(r, points.data(), static_cast<int>(point_count), width);
}

void drawTrigger(SDL_Renderer *r, SDL_Rect plot, ScopeState *state) {
  const float vdiv = std::max(0.02f, state->volts_per_div.load());
  const float trig_y = plot.y + plot.h * 0.5f -
                       state->trigger_level.load() * ((plot.h / 8.0f) / vdiv);
  setColor(r, {255, 150, 55, 210});
  thickLine(r, plot.x - 8, trig_y, plot.x + plot.w + 8, trig_y, 1);
  drawText(r, plot.x + plot.w - 64, static_cast<int>(trig_y) - 16, "TRIG",
           {255, 170, 80, 210}, 1);
}

void drawTopBar(SDL_Renderer *r, const Args &args, ScopeState *state, const DisplayFrame &frame,
                double render_fps, int w) {
  fillRect(r, SDL_Rect{0, 0, w, 58}, {7, 10, 12, 248});
  drawRect(r, SDL_Rect{0, 57, w, 1}, {48, 58, 64, 180});
  Color green{65, 255, 126, 255};
  Color amber{255, 190, 80, 255};
  Color text{205, 222, 220, 240};
  Color dim{130, 148, 150, 210};

  SDL_Rect run{18, 14, 78, 28};
  fillRect(r, run, state->capture.load() ? Color{16, 80, 42, 255} : Color{90, 60, 20, 255});
  drawRect(r, run, state->capture.load() ? green : amber);
  drawText(r, run.x + 17, run.y + 7, state->capture.load() ? "RUN" : "HOLD", green, 2);

  SDL_Rect trig{112, 14, 104, 28};
  fillRect(r, trig, frame.triggered ? Color{58, 44, 12, 255} : Color{24, 29, 32, 255});
  drawRect(r, trig, frame.triggered ? amber : Color{80, 94, 98, 210});
  drawText(r, trig.x + 12, trig.y + 7,
           frame.triggered ? "TRIG'D" : (state->trigger_auto.load() ? "AUTO" : "WAIT"),
           frame.triggered ? amber : dim, 2);

  char buf[64];
  int x = 250;
  std::snprintf(buf, sizeof(buf), "FS %.1fKS/S", args.sample_rate / 1000.0f);
  drawText(r, x, 18, buf, text, 2);
  x += 178;
  std::snprintf(buf, sizeof(buf), "%.3GMS/DIV", state->time_per_div.load() * 1000.0f);
  drawText(r, x, 18, buf, text, 2);
  x += 188;
  std::snprintf(buf, sizeof(buf), "%.0FMV/DIV", state->volts_per_div.load() * 1000.0f);
  drawText(r, x, 18, buf, text, 2);
  x += 178;
  std::snprintf(buf, sizeof(buf), "MEM %.1FK", kRingSize / 1024.0f);
  drawText(r, x, 18, buf, dim, 2);
  x += 150;
  std::snprintf(buf, sizeof(buf), "FPS %.0f", render_fps);
  drawText(r, x, 18, buf, render_fps >= 55.0 ? green : amber, 2);
}

void drawSidePanel(SDL_Renderer *r, const Args &args, ScopeState *state, SDL_Rect panel) {
  if (!state->panel_open.load()) return;
  fillRect(r, panel, {9, 13, 16, 238});
  drawRect(r, panel, {52, 64, 70, 170});
  Color title{185, 205, 202, 235};
  Color dim{120, 140, 145, 220};
  int selected = state->selected_channel.load();
  int y = panel.y + 18;
  drawText(r, panel.x + 16, y, "CHANNELS", title, 2);
  y += 30;
  for (int ch = 0; ch < args.channels; ++ch) {
    Color c = ch == selected ? kTraceColors[ch] : scaled(kTraceColors[ch], 0.68f, 215);
    if (ch == selected) fillRect(r, SDL_Rect{panel.x + 10, y - 5, panel.w - 20, 24}, {28, 36, 38, 230});
    fillRect(r, SDL_Rect{panel.x + 16, y + 2, 14, 14}, c);
    char label[32];
    std::snprintf(label, sizeof(label), "CH%d %s", ch + 1,
                  state->show_channel[ch].load() ? "ON" : "OFF");
    drawText(r, panel.x + 40, y, label, state->show_channel[ch].load() ? c : dim, 2);
    y += 25;
  }
  y += 16;
  drawText(r, panel.x + 16, y, "TRIGGER", title, 2);
  y += 28;
  drawText(r, panel.x + 16, y, "EDGE", dim, 2);
  drawText(r, panel.x + 92, y, state->trigger_rising.load() ? "RISING" : "FALL", title, 2);
  y += 24;
  char buf[48];
  std::snprintf(buf, sizeof(buf), "LEVEL %.2F", state->trigger_level.load());
  drawText(r, panel.x + 16, y, buf, dim, 2);
  y += 42;
  drawText(r, panel.x + 16, y, "ACQUISITION", title, 2);
  y += 28;
  std::snprintf(buf, sizeof(buf), "RATE %d", args.sample_rate);
  drawText(r, panel.x + 16, y, buf, dim, 2);
  y += 24;
  std::snprintf(buf, sizeof(buf), "BUFFER %uK", static_cast<unsigned>(kRingSize / 1024));
  drawText(r, panel.x + 16, y, buf, dim, 2);
  y += 24;
  drawText(r, panel.x + 16, y, viewModeName(state->view_mode.load()), dim, 2);
}

void drawBottomBar(SDL_Renderer *r, const Args &args, ScopeState *state,
                   const DisplayFrame &frame, int w, int h) {
  fillRect(r, SDL_Rect{0, h - 62, w, 62}, {7, 10, 12, 248});
  drawRect(r, SDL_Rect{0, h - 63, w, 1}, {48, 58, 64, 180});
  const int ch = std::clamp(state->selected_channel.load(), 0, args.channels - 1);
  const Measurements &m = frame.measurements[ch];
  Color c = kTraceColors[ch];
  fillRect(r, SDL_Rect{20, h - 42, 18, 18}, c);
  char buf[192];
  std::snprintf(buf, sizeof(buf),
                "CH%d %s   FREQ %.2FHZ   RMS %.3F   PEAK %.3F   VPP %.3F   PHASE %.1F   THD %.1F",
                ch + 1, kNames[ch], m.freq, m.rms, m.peak, m.vpp, m.phase, m.thd);
  drawText(r, 52, h - 44, buf, {205, 222, 220, 238}, 2);
}

void drawTraceLayer(SDL_Renderer *r, const Args &args, ScopeState *state, const DisplayFrame &frame,
                    SDL_Rect plot, uint8_t alpha, int width, bool glow) {
  const int mode = state->view_mode.load();
  const int selected = state->selected_channel.load();
  for (int ch = 0; ch < args.channels; ++ch) {
    if (!state->show_channel[ch].load()) continue;
    SDL_Rect area = plotForChannel(plot, ch, mode);
    const float peak = groupPeak(frame, ch, mode, args.channels);
    const bool is_selected = ch == selected;
    Color color = is_selected ? kTraceColors[ch] : scaled(kTraceColors[ch], 0.72f, alpha);
    color.a = is_selected ? std::min<int>(255, alpha + 35) : alpha;
    drawTrace(r, frame, ch, area, state->volts_per_div.load(), peak, color,
              is_selected ? width + 1 : width);
    if (glow && is_selected) {
      drawTrace(r, frame, ch, area, state->volts_per_div.load(), peak,
                scaled(kTraceColors[ch], 1.0f, 50), width + 3);
    }
  }
}

void drawUi(SDL_Renderer *r, const Args &args, ScopeState *state, const DisplayFrame &frame,
            const DisplayFrame &previous, RenderCache *cache, double render_fps) {
  int w = 0, h = 0;
  SDL_GetRendererOutputSize(r, &w, &h);
  fillRect(r, SDL_Rect{0, 0, w, h}, {0, 0, 0, 255});

  const int panel_w = state->panel_open.load() ? 250 : 0;
  SDL_Rect plot{70, 74, w - 100 - panel_w, h - 150};
  fillRect(r, plot, {0, 0, 0, 255});
  drawGrid(r, cache, plot, state->show_grid.load());

  if (state->persistence.load()) drawTraceLayer(r, args, state, previous, plot, 28, 1, false);
  drawTraceLayer(r, args, state, frame, plot, 245, 1, false);
  drawTrigger(r, plot, state);

  if (state->view_mode.load() == VIEW_SPLIT) {
    drawText(r, plot.x + 12, plot.y + 10, "VOLTAGE", {112, 132, 136, 150}, 2);
    drawText(r, plot.x + 12, plot.y + plot.h / 2 + 18, "CURRENT", {112, 132, 136, 150}, 2);
  }
  drawTopBar(r, args, state, frame, render_fps, w);
  drawSidePanel(r, args, state, SDL_Rect{w - panel_w + 8, 74, std::max(0, panel_w - 18), h - 150});
  drawBottomBar(r, args, state, frame, w, h);
}

struct InteractionState {
  bool dragging = false;
  int last_x = 0;
  int last_y = 0;
};

void autoscaleFromFrame(ScopeState *state, const DisplayFrame &frame, int channels) {
  float peak = 0.05f;
  for (int ch = 0; ch < channels; ++ch) {
    if (!state->show_channel[ch].load()) continue;
    for (size_t i = 0; i < frame.count; ++i) peak = std::max(peak, std::fabs(frame.samples[i].ch[ch]));
  }
  const float divs_for_peak = 2.6f;
  state->volts_per_div = std::clamp(peak / divs_for_peak, 0.02f, 1.0f);
}

void handleEvent(const SDL_Event &event, ScopeState *state, const Args &args,
                 InteractionState *interaction, const DisplayFrame &frame) {
  if (event.type == SDL_MOUSEWHEEL) {
    const bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
    const float factor = event.wheel.y > 0 ? 0.86f : 1.16f;
    if (shift) state->volts_per_div = std::clamp(state->volts_per_div.load() * factor, 0.02f, 2.0f);
    else state->time_per_div = std::clamp(state->time_per_div.load() * factor, 0.001f, 0.1f);
    return;
  }
  if (event.type == SDL_MOUSEBUTTONDOWN) {
    if (event.button.button == SDL_BUTTON_LEFT) {
      if (event.button.clicks >= 2) {
        autoscaleFromFrame(state, frame, args.channels);
      } else {
        interaction->dragging = true;
        interaction->last_x = event.button.x;
        interaction->last_y = event.button.y;
      }
    }
    return;
  }
  if (event.type == SDL_MOUSEBUTTONUP) {
    if (event.button.button == SDL_BUTTON_LEFT) interaction->dragging = false;
    return;
  }
  if (event.type == SDL_MOUSEMOTION && interaction->dragging) {
    const int dx = event.motion.x - interaction->last_x;
    const int dy = event.motion.y - interaction->last_y;
    interaction->last_x = event.motion.x;
    interaction->last_y = event.motion.y;
    state->horizontal_position =
        std::clamp(state->horizontal_position.load() - dx * 0.0015f, 0.08f, 0.85f);
    state->trigger_level =
        std::clamp(state->trigger_level.load() - dy * 0.0025f, -1.0f, 1.0f);
    return;
  }
  if (event.type != SDL_KEYDOWN) return;
  SDL_Keycode key = event.key.keysym.sym;
  if (key == SDLK_ESCAPE || key == SDLK_q) state->running = false;
  if (key == SDLK_SPACE) state->capture = !state->capture.load();
  if (key == SDLK_t) state->trigger_auto = !state->trigger_auto.load();
  if (key == SDLK_g) state->show_grid = !state->show_grid.load();
  if (key == SDLK_p) state->persistence = !state->persistence.load();
  if (key == SDLK_m || key == SDLK_v) {
    state->view_mode = (state->view_mode.load() + 1) % 3;
  }
  if (key == SDLK_o) state->panel_open = !state->panel_open.load();
  if (key == SDLK_a) autoscaleFromFrame(state, frame, args.channels);
  if (key == SDLK_e) state->trigger_rising = !state->trigger_rising.load();
  if (key == SDLK_LEFTBRACKET) state->trigger_level = state->trigger_level.load() - 0.05f;
  if (key == SDLK_RIGHTBRACKET) state->trigger_level = state->trigger_level.load() + 0.05f;
  if (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS) {
    state->time_per_div = state->time_per_div.load() * 0.8f;
  }
  if (key == SDLK_MINUS) state->time_per_div = state->time_per_div.load() * 1.25f;
  if (key == SDLK_UP) state->volts_per_div = state->volts_per_div.load() * 0.8f;
  if (key == SDLK_DOWN) state->volts_per_div = state->volts_per_div.load() * 1.25f;
  if (key == SDLK_TAB) {
    int next = (state->selected_channel.load() + 1) % args.channels;
    state->selected_channel = next;
    state->trigger_channel = next;
  }
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
  uint32_t renderer_flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;
  if (args.vsync) renderer_flags |= SDL_RENDERER_PRESENTVSYNC;
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, renderer_flags);
  if (!window || !renderer) {
    state->running = false;
    SDL_Quit();
    return;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

  auto last = std::chrono::steady_clock::now();
  int frames = 0;
  double render_fps = 0.0;
  RenderCache cache;
  InteractionState interaction;
  while (state->running.load()) {
    SDL_Event event;
    int front = state->front_display.load(std::memory_order_acquire);
    int previous = 1 - front;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) state->running = false;
      handleEvent(event, state, args, &interaction, (*display)[front]);
    }
    drawUi(renderer, args, state, (*display)[front], (*display)[previous], &cache, render_fps);
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
  destroyCache(&cache);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

void usage(const char *argv0) {
  std::cerr << "Usage: " << argv0
            << " [--spi-dev /dev/spidev0.0] [--serial /dev/ttyUSB0]"
            << " [--spi-hz 10000000] [--spi-mode 0] [--bits 24]"
            << " [--rate 4000] [--channels 8] [--drdy-gpio 4]"
            << " [--no-drdy] [--windowed] [--phosphor] [--vsync]\n";
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
    } else if (a == "--vsync") {
      args.vsync = true;
    } else if (a == "--no-vsync") {
      args.vsync = false;
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
  state.persistence = args.phosphor;

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
