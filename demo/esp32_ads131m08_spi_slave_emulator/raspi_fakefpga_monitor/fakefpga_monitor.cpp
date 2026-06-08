#include <SDL2/SDL.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace {

struct Color {
  uint8_t r, g, b, a;
};

struct Args {
  std::string serial_dev;
  bool fullscreen = true;
};

struct Telemetry {
  uint64_t seq = 0;
  float fps = 0.0f;
  uint32_t bad = 0;
  float freq = 0.0f;
  float va = 0.0f;
  float vb = 0.0f;
  float vc = 0.0f;
  float ia = 0.0f;
  float ib = 0.0f;
  float ic = 0.0f;
  float p = 0.0f;
  float q = 0.0f;
  float pf = 0.0f;
  float vunb = 0.0f;
  float field = 0.0f;
  bool run = false;
  bool automatic = true;
  bool fault = false;
  std::chrono::steady_clock::time_point last_rx = std::chrono::steady_clock::now();
};

struct Shared {
  std::atomic<bool> running{true};
  std::mutex mutex;
  Telemetry telemetry;
  std::string last_line = "waiting for FakeFPGA telemetry";
};

void setColor(SDL_Renderer *r, Color c) {
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

void fillRect(SDL_Renderer *r, SDL_Rect rect, Color color) {
  setColor(r, color);
  SDL_RenderFillRect(r, &rect);
}

void drawRect(SDL_Renderer *r, SDL_Rect rect, Color color) {
  setColor(r, color);
  SDL_RenderDrawRect(r, &rect);
}

const uint8_t kFont[37][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    {0x0F, 0x10, 0x10, 0x13, 0x11, 0x11, 0x0F}, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E},
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04, 0x04},
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C},
};

int fontIndex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
  if (c >= 'a' && c <= 'z') return 10 + (c - 'a');
  if (c == '.') return 36;
  return -1;
}

void drawText(SDL_Renderer *r, int x, int y, const std::string &text, Color color, int scale = 2) {
  setColor(r, color);
  int cx = x;
  for (char c : text) {
    if (c == ' ') {
      cx += 4 * scale;
      continue;
    }
    if (c == '-' || c == ':' || c == '%' || c == '/') {
      SDL_Rect rect{cx, y + 3 * scale, 4 * scale, scale};
      SDL_RenderFillRect(r, &rect);
      cx += 6 * scale;
      continue;
    }
    int idx = fontIndex(c);
    if (idx >= 0) {
      for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
          if (kFont[idx][row] & (1 << (4 - col))) {
            SDL_Rect rect{cx + col * scale, y + row * scale, scale, scale};
            SDL_RenderFillRect(r, &rect);
          }
        }
      }
    }
    cx += 6 * scale;
  }
}

int openSerial(const std::string &path) {
  int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) return -1;
  termios tty{};
  if (tcgetattr(fd, &tty) != 0) {
    ::close(fd);
    return -1;
  }
  cfmakeraw(&tty);
  cfsetispeed(&tty, B115200);
  cfsetospeed(&tty, B115200);
  tty.c_cflag |= CLOCAL | CREAD;
  tty.c_cflag &= ~CRTSCTS;
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 0;
  tcsetattr(fd, TCSANOW, &tty);
  return fd;
}

std::string autoSerialPort() {
  for (const char *prefix : {"/dev/ttyUSB", "/dev/ttyACM"}) {
    for (int i = 0; i < 8; ++i) {
      std::string path = std::string(prefix) + std::to_string(i);
      if (std::filesystem::exists(path)) return path;
    }
  }
  return "/dev/ttyUSB0";
}

void parseTelemetry(const std::string &line, Telemetry *t) {
  if (line.rfind("FPGA ", 0) != 0) return;
  std::istringstream ss(line.substr(5));
  std::string token;
  while (ss >> token) {
    size_t eq = token.find('=');
    if (eq == std::string::npos) continue;
    std::string key = token.substr(0, eq);
    std::string value = token.substr(eq + 1);
    float f = std::strtof(value.c_str(), nullptr);
    if (key == "seq") t->seq = std::strtoull(value.c_str(), nullptr, 10);
    else if (key == "fps") t->fps = f;
    else if (key == "bad") t->bad = std::strtoul(value.c_str(), nullptr, 10);
    else if (key == "f") t->freq = f;
    else if (key == "va") t->va = f;
    else if (key == "vb") t->vb = f;
    else if (key == "vc") t->vc = f;
    else if (key == "ia") t->ia = f;
    else if (key == "ib") t->ib = f;
    else if (key == "ic") t->ic = f;
    else if (key == "p") t->p = f;
    else if (key == "q") t->q = f;
    else if (key == "pf") t->pf = f;
    else if (key == "vunb") t->vunb = f;
    else if (key == "field") t->field = f;
    else if (key == "run") t->run = value == "1";
    else if (key == "auto") t->automatic = value == "1";
    else if (key == "fault") t->fault = value == "1";
  }
  t->last_rx = std::chrono::steady_clock::now();
}

void serialThread(std::string path, Shared *shared) {
  int fd = openSerial(path);
  if (fd < 0) {
    std::lock_guard<std::mutex> lock(shared->mutex);
    shared->last_line = "serial open failed: " + path;
    return;
  }
  std::string line;
  char buf[128];
  while (shared->running.load()) {
    ssize_t n = ::read(fd, buf, sizeof(buf));
    if (n > 0) {
      for (ssize_t i = 0; i < n; ++i) {
        char c = buf[i];
        if (c == '\n' || c == '\r') {
          if (!line.empty()) {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->last_line = line;
            parseTelemetry(line, &shared->telemetry);
            line.clear();
          }
        } else if (line.size() < 240) {
          line.push_back(c);
        }
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  ::close(fd);
}

void drawCard(SDL_Renderer *r, int x, int y, int w, int h, const std::string &title,
              const std::string &value, Color accent) {
  fillRect(r, SDL_Rect{x, y, w, h}, {10, 15, 17, 245});
  drawRect(r, SDL_Rect{x, y, w, h}, {42, 55, 58, 180});
  fillRect(r, SDL_Rect{x, y, 5, h}, accent);
  drawText(r, x + 18, y + 14, title, {120, 145, 145, 220}, 2);
  drawText(r, x + 18, y + 48, value, {218, 238, 232, 245}, 3);
}

void render(SDL_Renderer *r, const Telemetry &t, const std::string &line, int w, int h) {
  fillRect(r, SDL_Rect{0, 0, w, h}, {0, 0, 0, 255});
  fillRect(r, SDL_Rect{0, 0, w, 64}, {6, 10, 12, 250});
  drawText(r, 24, 20, "FAKEFPGA MOTOR CONTROL", {210, 232, 230, 245}, 3);

  SDL_Rect exit_btn{w - 104, 16, 82, 32};
  fillRect(r, exit_btn, {72, 18, 18, 245});
  drawRect(r, exit_btn, {210, 70, 70, 230});
  drawText(r, exit_btn.x + 15, exit_btn.y + 9, "EXIT", {255, 178, 170, 245}, 2);

  const bool stale = std::chrono::duration<double>(
                         std::chrono::steady_clock::now() - t.last_rx).count() > 1.5;
  Color ok = stale ? Color{255, 190, 80, 255} : Color{80, 255, 128, 255};
  Color fault = t.fault ? Color{255, 70, 70, 255} : ok;
  char buf[64];

  int card_w = (w - 80) / 4;
  int y = 92;
  std::snprintf(buf, sizeof(buf), "%.3FHZ", t.freq);
  drawCard(r, 24, y, card_w, 104, "FREQUENCY", buf, ok);
  std::snprintf(buf, sizeof(buf), "VA %.3F", t.va);
  drawCard(r, 36 + card_w, y, card_w, 104, "VOLTAGE RMS", buf, {255, 220, 30, 255});
  std::snprintf(buf, sizeof(buf), "IA %.3F", t.ia);
  drawCard(r, 48 + card_w * 2, y, card_w, 104, "CURRENT RMS", buf, {255, 150, 40, 255});
  std::snprintf(buf, sizeof(buf), "%.3F", t.pf);
  drawCard(r, 60 + card_w * 3, y, card_w, 104, "POWER FACTOR", buf, ok);

  y += 124;
  std::snprintf(buf, sizeof(buf), "P %.3F", t.p);
  drawCard(r, 24, y, card_w, 104, "REAL POWER", buf, {110, 190, 255, 255});
  std::snprintf(buf, sizeof(buf), "Q %.3F", t.q);
  drawCard(r, 36 + card_w, y, card_w, 104, "REACTIVE", buf, {210, 110, 255, 255});
  std::snprintf(buf, sizeof(buf), "%.1F%%", t.vunb);
  drawCard(r, 48 + card_w * 2, y, card_w, 104, "V UNBALANCE", buf, t.vunb > 5.0f ? Color{255, 190, 80, 255} : ok);
  std::snprintf(buf, sizeof(buf), "%.1F%%", t.field * 100.0f);
  drawCard(r, 60 + card_w * 3, y, card_w, 104, "FIELD PWM", buf, {90, 230, 210, 255});

  y += 138;
  drawCard(r, 24, y, card_w, 104, "RUN STATE", t.run ? "RUN" : "STOP", t.run ? ok : Color{255, 190, 80, 255});
  drawCard(r, 36 + card_w, y, card_w, 104, "FIELD MODE", t.automatic ? "AUTO" : "MANUAL", {140, 180, 255, 255});
  std::snprintf(buf, sizeof(buf), "%.0FFPS", t.fps);
  drawCard(r, 48 + card_w * 2, y, card_w, 104, "ADS FRAMES", buf, ok);
  std::snprintf(buf, sizeof(buf), "%u", t.bad);
  drawCard(r, 60 + card_w * 3, y, card_w, 104, "BAD FRAMES", buf, fault);

  fillRect(r, SDL_Rect{24, h - 78, w - 48, 48}, {6, 10, 12, 245});
  drawRect(r, SDL_Rect{24, h - 78, w - 48, 48}, {42, 55, 58, 180});
  drawText(r, 42, h - 62, stale ? "LINK STALE" : "LINK OK", ok, 2);
  drawText(r, 200, h - 62, line.substr(0, 120), {110, 135, 135, 210}, 1);
}

Args parseArgs(int argc, char **argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto need = [&](const char *name) -> const char * {
      if (i + 1 >= argc) {
        std::cerr << name << " requires value\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--serial") args.serial_dev = need("--serial");
    else if (a == "--windowed") args.fullscreen = false;
    else if (a == "--fullscreen") args.fullscreen = true;
  }
  if (args.serial_dev.empty()) args.serial_dev = autoSerialPort();
  return args;
}

}  // namespace

int main(int argc, char **argv) {
  Args args = parseArgs(argc, argv);
  Shared shared;
  std::thread serial(serialThread, args.serial_dev, &shared);

  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
  SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 1;
  uint32_t flags = SDL_WINDOW_SHOWN;
  if (args.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  SDL_Window *window = SDL_CreateWindow("FakeFPGA Monitor", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, 1280, 720, flags);
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!window || !renderer) return 1;

  while (shared.running.load()) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) shared.running = false;
      if (event.type == SDL_KEYDOWN &&
          (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q)) {
        shared.running = false;
      }
      if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(renderer, &w, &h);
        SDL_Rect exit_btn{w - 104, 16, 82, 32};
        if (event.button.x >= exit_btn.x && event.button.x < exit_btn.x + exit_btn.w &&
            event.button.y >= exit_btn.y && event.button.y < exit_btn.y + exit_btn.h) {
          shared.running = false;
        }
      }
    }

    Telemetry t;
    std::string line;
    {
      std::lock_guard<std::mutex> lock(shared.mutex);
      t = shared.telemetry;
      line = shared.last_line;
    }
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    render(renderer, t, line, w, h);
    SDL_RenderPresent(renderer);
    SDL_Delay(16);
  }

  shared.running = false;
  if (serial.joinable()) serial.join();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
