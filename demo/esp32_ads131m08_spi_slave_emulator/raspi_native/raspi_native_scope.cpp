#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kChannels = 8;
constexpr double kAdc24FullScale = 8388607.0;
constexpr std::array<const char *, kChannels> kNames = {
    "VA", "VB", "VC", "VAN", "IA", "IB", "IC", "IN"};
constexpr std::array<const char *, 5> kModes = {
    "SINE", "COUNTER", "TRIANGLE", "RANDOM", "CONSTANT"};
constexpr std::array<int, 5> kRates = {1000, 4000, 8000, 16000, 32000};
constexpr std::array<int, 3> kBits = {16, 24, 32};

struct Args {
  std::string spi_dev = "/dev/spidev0.0";
  std::string serial_dev;
  uint32_t spi_hz = 10000000;
  uint8_t spi_mode = 0;
  int word_bits = 24;
  int sample_rate = 4000;
  double line_hz = 60.0;
  int cycles = 6;
  bool crc = false;
};

struct SampleFrame {
  uint32_t status = 0;
  std::array<int32_t, kChannels> raw{};
  std::array<double, kChannels> norm{};
};

struct TerminalSize {
  int cols = 100;
  int rows = 32;
};

TerminalSize terminalSize() {
  winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
    return {static_cast<int>(ws.ws_col), static_cast<int>(ws.ws_row)};
  }
  return {};
}

std::string autoSerialPort() {
  for (const auto &prefix : {"/dev/ttyUSB", "/dev/ttyACM"}) {
    for (int i = 0; i < 8; ++i) {
      std::string path = std::string(prefix) + std::to_string(i);
      if (std::filesystem::exists(path)) return path;
    }
  }
  return {};
}

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

uint32_t decodeUnsignedWord(const std::vector<uint8_t> &frame, size_t offset, int word_bits) {
  return readWord(frame, offset, word_bits);
}

speed_t baudToTermios(int baud) {
  switch (baud) {
    case 9600: return B9600;
    case 57600: return B57600;
    case 115200: return B115200;
    case 921600: return B921600;
    default: return B115200;
  }
}

class SerialPort {
 public:
  bool openPort(const std::string &path, int baud = 115200) {
    if (path.empty()) return false;
    fd_ = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) return false;

    termios tty{};
    if (tcgetattr(fd_, &tty) != 0) return false;
    cfmakeraw(&tty);
    cfsetispeed(&tty, baudToTermios(baud));
    cfsetospeed(&tty, baudToTermios(baud));
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;
    if (tcsetattr(fd_, TCSANOW, &tty) != 0) return false;
    path_ = path;
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    return true;
  }

  ~SerialPort() {
    if (fd_ >= 0) ::close(fd_);
  }

  bool ok() const { return fd_ >= 0; }
  const std::string &path() const { return path_; }

  std::string send(const std::string &cmd) {
    if (fd_ < 0) return "serial closed";
    std::string line = cmd + "\n";
    ::write(fd_, line.data(), line.size());
    tcdrain(fd_);
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    char buf[512];
    ssize_t n = ::read(fd_, buf, sizeof(buf) - 1);
    if (n <= 0) return "sent";
    buf[n] = '\0';
    last_response_ = buf;
    while (!last_response_.empty() &&
           (last_response_.back() == '\n' || last_response_.back() == '\r')) {
      last_response_.pop_back();
    }
    return last_response_;
  }

  const std::string &lastResponse() const { return last_response_; }

 private:
  int fd_ = -1;
  std::string path_;
  std::string last_response_;
};

class SpiDevice {
 public:
  bool openDevice(const Args &args) {
    fd_ = ::open(args.spi_dev.c_str(), O_RDWR);
    if (fd_ < 0) return false;
    uint8_t mode = args.spi_mode;
    uint8_t bits = 8;
    uint32_t hz = args.spi_hz;
    if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0) return false;
    if (ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) return false;
    if (ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &hz) < 0) return false;
    hz_ = hz;
    return true;
  }

  ~SpiDevice() {
    if (fd_ >= 0) ::close(fd_);
  }

  bool transfer(const std::vector<uint8_t> &tx, std::vector<uint8_t> &rx) {
    if (fd_ < 0) return false;
    rx.assign(tx.size(), 0);
    spi_ioc_transfer tr{};
    tr.tx_buf = reinterpret_cast<uintptr_t>(tx.data());
    tr.rx_buf = reinterpret_cast<uintptr_t>(rx.data());
    tr.len = static_cast<uint32_t>(tx.size());
    tr.speed_hz = hz_;
    tr.bits_per_word = 8;
    return ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) >= 0;
  }

 private:
  int fd_ = -1;
  uint32_t hz_ = 10000000;
};

class RawTerminal {
 public:
  RawTerminal() {
    if (tcgetattr(STDIN_FILENO, &saved_) == 0) {
      termios raw = saved_;
      raw.c_lflag &= ~(ICANON | ECHO);
      raw.c_cc[VMIN] = 0;
      raw.c_cc[VTIME] = 0;
      tcsetattr(STDIN_FILENO, TCSANOW, &raw);
      active_ = true;
    }
  }

  ~RawTerminal() {
    if (active_) tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
    std::cout << "\033[?25h\033[0m\n";
  }

 private:
  termios saved_{};
  bool active_ = false;
};

int readKey() {
  pollfd pfd{STDIN_FILENO, POLLIN, 0};
  if (poll(&pfd, 1, 0) <= 0) return -1;
  unsigned char c = 0;
  return ::read(STDIN_FILENO, &c, 1) == 1 ? c : -1;
}

std::string bar(double value, int width = 42) {
  value = std::clamp(value, -1.0, 1.0);
  int mid = width / 2;
  int pos = mid + static_cast<int>(std::round(value * mid));
  std::string s(width + 1, ' ');
  s[mid] = '|';
  if (pos >= mid) {
    for (int i = mid + 1; i <= pos && i <= width; ++i) s[i] = '#';
  } else {
    for (int i = pos; i < mid; ++i) s[i] = '#';
  }
  return s;
}

size_t windowSamples(const Args &args) {
  double samples = (static_cast<double>(args.sample_rate) * args.cycles) / args.line_hz;
  return static_cast<size_t>(std::max(32.0, std::ceil(samples)));
}

char traceChar(int channel) {
  static constexpr std::array<char, kChannels> chars = {
      'A', 'B', 'C', 'N', 'a', 'b', 'c', 'n'};
  return chars[channel];
}

std::vector<std::string> plotGroup(const std::deque<SampleFrame> &history,
                                   const std::array<int, 4> &channels,
                                   size_t wanted_samples, int width, int height) {
  std::vector<std::string> rows(static_cast<size_t>(height), std::string(width, ' '));
  if (history.empty()) return rows;

  int mid = height / 2;
  for (int x = 0; x < width; ++x) {
    rows[mid][x] = '-';
  }
  for (int y = 0; y < height; ++y) {
    rows[y][0] = '|';
  }

  size_t available = std::min(wanted_samples, history.size());
  size_t start = history.size() - available;
  for (int x = 0; x < width; ++x) {
    size_t rel = width <= 1 ? 0 : static_cast<size_t>(
        (static_cast<double>(x) * (available - 1)) / (width - 1));
    const SampleFrame &sample = history[start + rel];
    for (int ch : channels) {
      double v = std::clamp(sample.norm[ch], -1.0, 1.0);
      int y = static_cast<int>(std::round((1.0 - (v + 1.0) * 0.5) * (height - 1)));
      y = std::clamp(y, 0, height - 1);
      char &cell = rows[static_cast<size_t>(y)][x];
      cell = (cell == ' ' || cell == '-' || cell == '|') ? traceChar(ch) : '*';
    }
  }
  return rows;
}

void sendConfig(SerialPort &serial, const Args &args, const std::string &mode) {
  if (!serial.ok()) return;
  serial.send("BITS " + std::to_string(args.word_bits));
  serial.send("RATE " + std::to_string(args.sample_rate));
  serial.send("SPI " + std::to_string(args.spi_hz));
  serial.send("SPIMODE " + std::to_string(args.spi_mode));
  serial.send(std::string("CRC ") + (args.crc ? "ON" : "OFF"));
  serial.send("MODE " + mode);
  serial.send("START");
}

void render(const Args &args, const SerialPort &serial, const SampleFrame &sample,
            const std::deque<SampleFrame> &history,
            uint64_t frames, uint64_t errors, double fps, const std::string &mode,
            const std::string &message) {
  TerminalSize term = terminalSize();
  int graph_width = std::clamp(term.cols - 2, 40, 160);
  int reserved_rows = 16;  // compact header, labels, and 8 instant-value rows
  int available_graph_rows = std::max(8, term.rows - reserved_rows);
  int graph_height = std::clamp(available_graph_rows / 2, 4, 13);

  std::cout << "\033[H\033[2J\033[?25l";
  std::cout << "ESP32 ADS131M08 native Raspberry scope\n";
  std::cout << "SPI " << args.spi_dev << " mode=" << static_cast<int>(args.spi_mode)
            << " hz=" << args.spi_hz << " word=" << args.word_bits
            << " target=" << args.sample_rate << "SPS"
            << " serial=" << (serial.ok() ? serial.path() : "none") << "\n";
  std::cout << "frames=" << frames << " errors=" << errors << " fps=" << std::fixed
            << std::setprecision(1) << fps << " status=0x" << std::hex << sample.status
            << std::dec << " mode=" << mode << "\n";
  std::cout << "window=" << args.cycles << " cycles @ " << std::fixed
            << std::setprecision(1) << args.line_hz << "Hz"
            << " samples=" << windowSamples(args) << "\n";
  std::cout << "keys: q quit | s/x start/stop | m mode | r rate | b bits | +/- cycles | c config\n";
  std::cout << "last: " << (message.empty() ? "-" : message) << "\n";

  auto voltage = plotGroup(history, {0, 1, 2, 3}, windowSamples(args), graph_width, graph_height);
  auto current = plotGroup(history, {4, 5, 6, 7}, windowSamples(args), graph_width, graph_height);

  std::cout << "Voltages: A=VA B=VB C=VC N=VAN  (* overlap)\n";
  for (const auto &row : voltage) std::cout << row << "\n";
  std::cout << "Currents: a=IA b=IB c=IC n=IN  (* overlap)\n";
  for (const auto &row : current) std::cout << row << "\n";
  std::cout << "Instant values\n";

  for (int i = 0; i < kChannels; ++i) {
    std::cout << kNames[i] << (std::strlen(kNames[i]) < 3 ? "  " : " ")
              << std::showpos << std::fixed << std::setprecision(4) << sample.norm[i]
              << std::noshowpos << " " << bar(sample.norm[i]) << " raw="
              << sample.raw[i] << "\n";
  }
  std::cout.flush();
}

bool decodeFrame(const std::vector<uint8_t> &rx, const Args &args, SampleFrame &out) {
  const size_t word_bytes = static_cast<size_t>(args.word_bits / 8);
  const size_t expected = (1 + kChannels + (args.crc ? 1 : 0)) * word_bytes;
  if (rx.size() != expected) return false;
  if (args.crc) {
    const size_t payload_len = expected - word_bytes;
    const uint16_t got = static_cast<uint16_t>(
        decodeUnsignedWord(rx, payload_len, args.word_bits) & 0xFFFF);
    if (crc16Ccitt(rx, payload_len) != got) return false;
  }
  out.status = decodeUnsignedWord(rx, 0, args.word_bits);
  for (int ch = 0; ch < kChannels; ++ch) {
    out.raw[ch] = decodeSignedWord(rx, (ch + 1) * word_bytes, args.word_bits);
    out.norm[ch] = std::clamp(out.raw[ch] / kAdc24FullScale, -1.0, 1.0);
  }
  return true;
}

void usage(const char *argv0) {
  std::cerr << "Usage: " << argv0
            << " [--spi-dev /dev/spidev0.0] [--serial /dev/ttyUSB0]"
            << " [--spi-hz 10000000] [--spi-mode 0] [--bits 24]"
            << " [--rate 4000] [--line-hz 60] [--cycles 6] [--crc]\n";
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
    } else if (a == "--line-hz") {
      const char *v = need("--line-hz");
      if (!v) return false;
      args.line_hz = std::atof(v);
    } else if (a == "--cycles") {
      const char *v = need("--cycles");
      if (!v) return false;
      args.cycles = std::atoi(v);
    } else if (a == "--crc") {
      args.crc = true;
    } else if (a == "--help" || a == "-h") {
      usage(argv[0]);
      return false;
    } else {
      std::cerr << "Unknown arg: " << a << "\n";
      return false;
    }
  }
  return (args.word_bits == 16 || args.word_bits == 24 || args.word_bits == 32) &&
         args.line_hz > 0.0 && args.cycles > 0;
}

}  // namespace

int main(int argc, char **argv) {
  Args args;
  if (!parseArgs(argc, argv, args)) return 2;
  if (args.serial_dev.empty()) args.serial_dev = autoSerialPort();

  SpiDevice spi;
  if (!spi.openDevice(args)) {
    std::perror("open/configure SPI");
    return 1;
  }

  SerialPort serial;
  serial.openPort(args.serial_dev);

  std::string mode = "SINE";
  sendConfig(serial, args, mode);

  RawTerminal terminal;
  SampleFrame sample;
  std::deque<SampleFrame> history;
  uint64_t frames = 0;
  uint64_t errors = 0;
  uint64_t frames_at_last = 0;
  double fps = 0.0;
  std::string message = serial.ok() ? serial.lastResponse() : "serial not available";

  auto next_sample = std::chrono::steady_clock::now();
  auto last_render = next_sample;
  auto last_fps = next_sample;
  std::vector<uint8_t> tx;
  std::vector<uint8_t> rx;

  size_t mode_index = 0;
  size_t rate_index = 1;
  size_t bits_index = 1;

  while (true) {
    int key = readKey();
    if (key == 'q' || key == 'Q') break;
    if (key == 's' || key == 'S') message = serial.send("START");
    if (key == 'x' || key == 'X') message = serial.send("STOP");
    if (key == 'c' || key == 'C') message = serial.send("CONFIG");
    if (key == 'm' || key == 'M') {
      mode_index = (mode_index + 1) % kModes.size();
      mode = kModes[mode_index];
      message = serial.send("MODE " + mode);
    }
    if (key == 'r' || key == 'R') {
      rate_index = (rate_index + 1) % kRates.size();
      args.sample_rate = kRates[rate_index];
      message = serial.send("RATE " + std::to_string(args.sample_rate));
    }
    if (key == 'b' || key == 'B') {
      bits_index = (bits_index + 1) % kBits.size();
      args.word_bits = kBits[bits_index];
      message = serial.send("BITS " + std::to_string(args.word_bits));
    }
    if (key == '+') args.cycles = std::min(20, args.cycles + 1);
    if (key == '-') args.cycles = std::max(1, args.cycles - 1);

    const size_t frame_bytes =
        static_cast<size_t>(1 + kChannels + (args.crc ? 1 : 0)) * (args.word_bits / 8);
    tx.assign(frame_bytes, 0);
    if (spi.transfer(tx, rx) && decodeFrame(rx, args, sample)) {
      ++frames;
      history.push_back(sample);
      size_t keep = std::max<size_t>(windowSamples(args) * 2, 256);
      while (history.size() > keep) history.pop_front();
    } else {
      ++errors;
    }

    auto now = std::chrono::steady_clock::now();
    if (now - last_fps >= std::chrono::seconds(1)) {
      double seconds = std::chrono::duration<double>(now - last_fps).count();
      fps = (frames - frames_at_last) / seconds;
      frames_at_last = frames;
      last_fps = now;
    }
    if (now - last_render >= std::chrono::milliseconds(80)) {
      render(args, serial, sample, history, frames, errors, fps, mode, message);
      last_render = now;
    }

    next_sample += std::chrono::microseconds(1000000 / std::max(1, args.sample_rate));
    std::this_thread::sleep_until(next_sample);
    if (std::chrono::steady_clock::now() > next_sample + std::chrono::milliseconds(50)) {
      next_sample = std::chrono::steady_clock::now();
    }
  }

  if (serial.ok()) serial.send("STOP");
  return 0;
}
