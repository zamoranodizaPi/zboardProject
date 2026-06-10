#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <sys/stat.h>
#include <string>
#include <termios.h>
#include <thread>
#include <ctime>
#include <unistd.h>
#include <vector>

#ifdef USE_GPIOD
#include <gpiod.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

std::atomic<bool> g_running{true};
constexpr float kPi = 3.14159265358979323846f;
constexpr float kLineHz = 60.0f;

enum class PlantState {
  Stopped,
  Energized,
  Accelerating,
  NearSync,
  FieldApplied,
  Synchronized,
  Loaded,
  Coasting,
  Faulted
};

enum class PlantScenario {
  Normal,
  NoDischarge,
  NoField,
  ThermalTrip,
  Pullout,
  GateFail,
  ScrOpen,
  ScrShort
};

const char *stateName(PlantState state) {
  switch (state) {
    case PlantState::Stopped: return "STOPPED";
    case PlantState::Energized: return "ENERGIZED";
    case PlantState::Accelerating: return "ACCELERATING";
    case PlantState::NearSync: return "NEAR_SYNC";
    case PlantState::FieldApplied: return "FIELD_APPLIED";
    case PlantState::Synchronized: return "SYNCHRONIZED";
    case PlantState::Loaded: return "LOADED";
    case PlantState::Coasting: return "COASTING";
    case PlantState::Faulted: return "FAULTED";
  }
  return "UNKNOWN";
}

int stateId(PlantState state) {
  switch (state) {
    case PlantState::Stopped: return 0;
    case PlantState::Energized: return 1;
    case PlantState::Accelerating: return 2;
    case PlantState::NearSync: return 3;
    case PlantState::FieldApplied: return 4;
    case PlantState::Synchronized: return 5;
    case PlantState::Loaded: return 6;
    case PlantState::Coasting: return 7;
    case PlantState::Faulted: return 8;
  }
  return -1;
}

const char *scenarioName(PlantScenario scenario) {
  switch (scenario) {
    case PlantScenario::Normal: return "NORMAL";
    case PlantScenario::NoDischarge: return "NO_DISCHARGE";
    case PlantScenario::NoField: return "NO_FIELD";
    case PlantScenario::ThermalTrip: return "THERMAL_TRIP";
    case PlantScenario::Pullout: return "PULLOUT";
    case PlantScenario::GateFail: return "GATE_FAIL";
    case PlantScenario::ScrOpen: return "SCR_OPEN";
    case PlantScenario::ScrShort: return "SCR_SHORT";
  }
  return "NORMAL";
}

bool parseScenario(const std::string &text, PlantScenario &scenario) {
  std::string value = text;
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::toupper(c); });
  if (value == "NORMAL") scenario = PlantScenario::Normal;
  else if (value == "NO_DISCHARGE" || value == "NODISCH") scenario = PlantScenario::NoDischarge;
  else if (value == "NO_FIELD" || value == "NOFIELD") scenario = PlantScenario::NoField;
  else if (value == "THERMAL_TRIP" || value == "THERMAL") scenario = PlantScenario::ThermalTrip;
  else if (value == "PULLOUT") scenario = PlantScenario::Pullout;
  else if (value == "GATE_FAIL" || value == "GATEFAIL") scenario = PlantScenario::GateFail;
  else if (value == "SCR_OPEN" || value == "SCROPEN") scenario = PlantScenario::ScrOpen;
  else if (value == "SCR_SHORT" || value == "SCRSHORT") scenario = PlantScenario::ScrShort;
  else return false;
  return true;
}

struct Args {
  std::string chip = "gpiochip0";
  std::string adsSerial = "";
  std::string telemetryPath = "/tmp/nexus_motor_plant_state.json";
  std::string commandFifo = "/tmp/nexus_motor_plant_cmd";
  std::string fpgaStatePath = "/tmp/nexus_fpga_control_state.json";
  int tickMs = 5;
  int fieldEnableBcm = 26;
  bool dryRun = false;
  bool autoRun = false;
  bool pinTest = false;
  float loadPct = 45.0f;
};

struct InputPins {
  int motorRunCmd = 5;   // Pi BCM, physical 29
  int fieldPwmCmd = 6;   // Pi BCM, physical 31
  int fieldEnableCmd = 26; // Pi BCM, physical 37
  int syncPulse = 13;    // Pi BCM, physical 33
  int faultOut = 19;     // Pi BCM, physical 35
};

struct OutputPins {
  int breakerClosed = 17;     // Pi BCM, physical 11
  int speedOk = 22;           // Pi BCM, physical 15
  int fieldCurrent = 23;      // Pi BCM, physical 16
  int dischargeCurrent = 24;  // Pi BCM, physical 18
  int thermalOk = 25;         // Pi BCM, physical 22
  int exciterReady = 12;      // Pi BCM, physical 32
  int loadReady = 16;         // Pi BCM, physical 36
  int emergencyOk = 20;       // Pi BCM, physical 38
  int plantFault = 21;        // Pi BCM, physical 40
};

struct ControlIn {
  bool motorRunCmd = false;
  bool fieldEnable = false;
  bool fieldPwmSeen = false;
  bool syncPulse = false;
  bool faultOut = false;
};

struct FpgaControlFallback {
  bool valid = false;
  bool motorRun = false;
  bool fieldEnable = false;
  bool fieldPwmSeen = false;
  bool faultOut = false;
};

struct FeedbackOut {
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

struct ExciterBoardInput {
  bool breakerClosed = false;
  bool fieldEnableCmd = false;
  bool controllerFault = false;
  float speedPct = 0.0f;
  float loadPct = 0.0f;
  PlantScenario scenario = PlantScenario::Normal;
  float motorFieldCurrentA = 0.0f;
};

struct ExciterBoardOutput {
  float dcBusVoltageV = 0.0f;
  float fieldVoltageV = 0.0f;
  float fieldCurrentTargetA = 0.0f;
  float dischargeCurrentA = 0.0f;
  float rectifierAverageV = 0.0f;
  float rectifierRippleV = 0.0f;
  bool fieldApplied = false;
  bool rectifierReady = false;
  bool rf3Enabled = false;
  bool dischargeActive = false;
  bool crowbarActive = false;
  bool bleedActive = false;
};

struct ExciterBoard {
  float electricalAngleDeg = 0.0f;
  float firingAngleDeg = 125.0f;
  float gatePulseWidthDeg = 8.0f;
  float dcBusVoltageV = 0.0f;
  float dcFilterVoltageV = 0.0f;
  float rectifierAverageV = 0.0f;
  float rectifierInstantV = 0.0f;
  float rectifierRippleV = 0.0f;
  float rectifierPhaseVa = 0.0f;
  float rectifierPhaseVb = 0.0f;
  float rectifierPhaseVc = 0.0f;
  float fieldInductanceH = 3.5f;
  float fieldResistanceOhm = 22.0f;
  float dischargeResistanceOhm = 38.0f;
  float bleedResistanceOhm = 85.0f;
  float crowbarResistanceOhm = 8.0f;
  float fieldVoltageTransducerV = 0.0f;
  float fieldCurrentTransducerA = 0.0f;
  float dischargeCurrentTransducerA = 0.0f;
  float powerFactorSignal = 0.0f;
  float commutationOverlapDeg = 0.0f;
  bool rf3Enabled = false;
  bool rectifierReady = true;
  bool powerFactorRegDisabled = false;
  bool bleedActive = false;
  bool dischargeActive = false;
  bool crowbarActive = false;
  bool scrGate[6] = {};
  bool scrGateExec[6] = {};
  bool scrConducting[6] = {};
  bool scrOpen[6] = {};
  bool scrShort[6] = {};
  bool gateFailed[6] = {};
  bool auxSk10 = false;
  bool auxSk11 = false;
  bool auxSk12 = false;
  bool auxSk13 = false;
  int activePair = 0;
};

struct Plant {
  PlantState state = PlantState::Stopped;
  float speedPct = 0.0f;
  float slipHz = 60.0f;
  float loadPct = 45.0f;
  float statorCurrentScale = 0.0f;
  float motorTorquePu = 0.0f;
  float loadTorquePu = 0.0f;
  float fieldCurrentA = 0.0f;
  float fieldVoltageV = 0.0f;
  float dischargeCurrentA = 0.0f;
  float pulloutRisk = 0.0f;
  bool breakerClosed = false;
  bool fieldApplied = false;
  bool fault = false;
  bool forceStop = false;
  PlantScenario scenario = PlantScenario::Normal;
  ExciterBoard exciter;
  ExciterBoardOutput exciterOut;
};

void onSignal(int) {
  g_running = false;
}

float clampf(float value, float lo, float hi) {
  return std::max(lo, std::min(value, hi));
}

float approach(float value, float target, float ratePerSecond, float dt) {
  const float step = ratePerSecond * dt;
  if (value < target) return std::min(value + step, target);
  return std::max(value - step, target);
}

float wrap360(float value) {
  while (value >= 360.0f) value -= 360.0f;
  while (value < 0.0f) value += 360.0f;
  return value;
}

float angularDistanceDeg(float a, float b) {
  float d = std::fabs(wrap360(a) - wrap360(b));
  return d > 180.0f ? 360.0f - d : d;
}

float firstOrder(float value, float target, float tauSeconds, float dt) {
  const float alpha = clampf(dt / std::max(0.001f, tauSeconds), 0.0f, 1.0f);
  return value + (target - value) * alpha;
}

ExciterBoardOutput updateExciterBoard(ExciterBoard &r, const ExciterBoardInput &in, float dt) {
  r.electricalAngleDeg = wrap360(r.electricalAngleDeg + 360.0f * kLineHz * dt);
  r.rectifierReady = !in.controllerFault && in.breakerClosed && in.scenario != PlantScenario::NoField;
  r.rf3Enabled = r.rectifierReady && in.fieldEnableCmd && in.speedPct > 75.0f;
  r.powerFactorRegDisabled = !r.rf3Enabled || in.scenario == PlantScenario::NoField;

  const float requestedField = r.rf3Enabled ? clampf((in.loadPct + 20.0f) / 120.0f, 0.0f, 1.0f) : 0.0f;
  r.firingAngleDeg = 135.0f - 105.0f * requestedField;
  if (in.scenario == PlantScenario::Pullout) r.firingAngleDeg = std::min(150.0f, r.firingAngleDeg + 18.0f);
  if (in.scenario == PlantScenario::NoField) r.firingAngleDeg = 170.0f;

  for (int i = 0; i < 6; ++i) {
    r.gateFailed[i] = false;
    r.scrOpen[i] = false;
    r.scrShort[i] = false;
  }
  if (in.scenario == PlantScenario::GateFail) r.gateFailed[0] = true;
  if (in.scenario == PlantScenario::ScrOpen) r.scrOpen[1] = true;
  if (in.scenario == PlantScenario::ScrShort) r.scrShort[2] = true;

  static const float gateBaseDeg[6] = {0.0f, 60.0f, 120.0f, 180.0f, 240.0f, 300.0f};
  static const int positiveLeg[6] = {0, 0, 1, 1, 2, 2};
  static const int negativeLeg[6] = {1, 2, 2, 0, 0, 1};
  const int naturalSector = static_cast<int>(wrap360(r.electricalAngleDeg - r.firingAngleDeg + 30.0f) / 60.0f) % 6;
  r.activePair = -1;
  bool forcedShort = false;
  for (int i = 0; i < 6; ++i) {
    const float gateAngle = wrap360(gateBaseDeg[i] + r.firingAngleDeg);
    r.scrGate[i] = r.rf3Enabled && angularDistanceDeg(r.electricalAngleDeg, gateAngle) <= r.gatePulseWidthDeg * 0.5f;
    r.scrGateExec[i] = r.scrGate[i] && !r.gateFailed[i] && !r.scrOpen[i];
    const bool inConductionSector = r.rf3Enabled && i == naturalSector;
    r.scrConducting[i] = (inConductionSector && !r.gateFailed[i] && !r.scrOpen[i]) || r.scrShort[i];
    if (r.scrShort[i]) forcedShort = true;
    if (r.scrConducting[i] && r.activePair < 0) r.activePair = i;
  }

  const float theta = r.electricalAngleDeg * kPi / 180.0f;
  const float phasePeakV = 98.0f; // Approx 120 Vac line-line secondary after scaling into exciter model.
  r.rectifierPhaseVa = phasePeakV * std::sin(theta);
  r.rectifierPhaseVb = phasePeakV * std::sin(theta - 2.0f * kPi / 3.0f);
  r.rectifierPhaseVc = phasePeakV * std::sin(theta + 2.0f * kPi / 3.0f);
  const float phases[3] = {r.rectifierPhaseVa, r.rectifierPhaseVb, r.rectifierPhaseVc};
  const float alphaRad = r.firingAngleDeg * kPi / 180.0f;
  const float vdo = 2.34f * phasePeakV; // 3-phase full controlled bridge average at alpha=0.
  r.rectifierAverageV = r.rf3Enabled ? std::max(0.0f, vdo * std::cos(alphaRad)) : 0.0f;
  if (forcedShort) r.rectifierAverageV = std::min(r.rectifierAverageV, 18.0f);
  if (in.scenario == PlantScenario::GateFail || in.scenario == PlantScenario::ScrOpen) {
    r.rectifierAverageV *= 0.68f;
  }
  if (r.activePair >= 0) {
    const int p = positiveLeg[r.activePair];
    const int n = negativeLeg[r.activePair];
    const float instantaneous = std::max(0.0f, phases[p] - phases[n]);
    const float overlapLoss = clampf(in.motorFieldCurrentA * 0.9f, 0.0f, 18.0f);
    r.rectifierInstantV = std::max(0.0f, instantaneous - overlapLoss);
  } else {
    r.rectifierInstantV = 0.0f;
  }
  const float rippleMagnitude = clampf(0.035f + in.motorFieldCurrentA * 0.008f, 0.035f, 0.16f);
  const float targetDc = r.rf3Enabled ? std::max(0.0f, 0.72f * r.rectifierInstantV + 0.28f * r.rectifierAverageV) : 0.0f;
  const float tau = r.rf3Enabled ? 0.018f : 0.045f;
  r.dcFilterVoltageV = firstOrder(r.dcFilterVoltageV, targetDc, tau, dt);
  const float ripple6 = rippleMagnitude * r.rectifierAverageV * std::sin(6.0f * theta);
  r.rectifierRippleV = r.rf3Enabled ? ripple6 : 0.0f;
  r.dcBusVoltageV = std::max(0.0f, r.dcFilterVoltageV + r.rectifierRippleV);
  r.commutationOverlapDeg = clampf(in.motorFieldCurrentA * 1.8f, 0.0f, 18.0f);
  r.dischargeActive = in.breakerClosed && !r.rf3Enabled && in.speedPct > 5.0f;
  r.bleedActive = !in.breakerClosed && in.motorFieldCurrentA > 0.05f;
  r.crowbarActive = (in.controllerFault || forcedShort) && in.motorFieldCurrentA > 0.5f;
  r.auxSk10 = r.dischargeActive;
  r.auxSk11 = r.dischargeActive || r.bleedActive;
  r.auxSk12 = r.crowbarActive;
  r.auxSk13 = r.crowbarActive;

  ExciterBoardOutput out;
  out.dcBusVoltageV = r.dcBusVoltageV;
  out.fieldApplied = r.rf3Enabled;
  out.rectifierReady = r.rectifierReady;
  out.rf3Enabled = r.rf3Enabled;
  out.dischargeActive = r.dischargeActive;
  out.crowbarActive = r.crowbarActive;
  out.bleedActive = r.bleedActive;
  out.rectifierAverageV = r.rectifierAverageV;
  out.rectifierRippleV = r.rectifierRippleV;
  out.fieldCurrentTargetA = r.rf3Enabled ? std::max(0.0f, r.rectifierAverageV / r.fieldResistanceOhm) : 0.0f;
  out.fieldVoltageV = r.rf3Enabled ? r.dcBusVoltageV : 0.0f;
  const float dischargeR = r.crowbarActive ? r.crowbarResistanceOhm : (r.dischargeActive ? r.dischargeResistanceOhm : r.bleedResistanceOhm);
  out.dischargeCurrentA = (r.dischargeActive || r.bleedActive || r.crowbarActive) ? in.motorFieldCurrentA * r.fieldResistanceOhm / std::max(1.0f, dischargeR) : 0.0f;
  r.fieldVoltageTransducerV = out.fieldVoltageV;
  r.fieldCurrentTransducerA = in.motorFieldCurrentA;
  r.dischargeCurrentTransducerA = out.dischargeCurrentA;
  r.powerFactorSignal = clampf(0.5f + (100.0f - r.firingAngleDeg) / 140.0f, 0.0f, 1.0f);
  return out;
}

class SerialPort {
 public:
  ~SerialPort() { close(); }

  bool openPort(const std::string &path, int baud = 115200) {
    if (path.empty()) return false;
    fd_ = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
      std::cerr << "serial open failed " << path << ": " << std::strerror(errno) << "\n";
      return false;
    }
    termios tty{};
    if (tcgetattr(fd_, &tty) != 0) return false;
    cfmakeraw(&tty);
    cfsetispeed(&tty, baudConstant(baud));
    cfsetospeed(&tty, baudConstant(baud));
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    if (tcsetattr(fd_, TCSANOW, &tty) != 0) return false;
    return true;
  }

  void close() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  bool writeLine(const std::string &line) {
    if (fd_ < 0) return false;
    const std::string out = line + "\n";
    const bool ok = ::write(fd_, out.data(), out.size()) == static_cast<ssize_t>(out.size());
    drainInput();
    return ok;
  }

 private:
  void drainInput() {
    if (fd_ < 0) return;
    char buf[128];
    for (;;) {
      const ssize_t n = ::read(fd_, buf, sizeof(buf));
      if (n <= 0) break;
    }
  }

  speed_t baudConstant(int baud) {
    switch (baud) {
      case 9600: return B9600;
      case 57600: return B57600;
      case 115200:
      default: return B115200;
    }
  }

  int fd_ = -1;
};

class Gpio {
 public:
  explicit Gpio(bool dryRun) : dryRun_(dryRun) {}
  ~Gpio() { close(); }

  bool openChip(const std::string &chipName) {
    if (dryRun_) return true;
#ifdef USE_GPIOD
    chip_ = gpiod_chip_open_by_name(chipName.c_str());
    if (!chip_) {
      std::cerr << "gpiod open failed for " << chipName << "\n";
      return false;
    }
    return true;
#else
    std::cerr << "libgpiod not available; use --dry-run or install libgpiod-dev\n";
    return false;
#endif
  }

  bool addInput(const std::string &name, int bcm) {
    if (dryRun_) {
      inputs_[name] = nullptr;
      return true;
    }
#ifdef USE_GPIOD
    gpiod_line *line = gpiod_chip_get_line(chip_, bcm);
    if (!line || gpiod_line_request_input(line, "nexus_motor_plant_sim") != 0) {
      std::cerr << "input request failed BCM" << bcm << " " << name << "\n";
      return false;
    }
    inputs_[name] = line;
    return true;
#else
    (void)name;
    (void)bcm;
    return false;
#endif
  }

  bool addOutput(const std::string &name, int bcm, bool initial) {
    outputValues_[name] = initial;
    if (dryRun_) {
      outputs_[name] = nullptr;
      return true;
    }
#ifdef USE_GPIOD
    gpiod_line *line = gpiod_chip_get_line(chip_, bcm);
    if (!line || gpiod_line_request_output(line, "nexus_motor_plant_sim", initial ? 1 : 0) != 0) {
      std::cerr << "output request failed BCM" << bcm << " " << name << "\n";
      return false;
    }
    outputs_[name] = line;
    return true;
#else
    (void)name;
    (void)bcm;
    return false;
#endif
  }

  bool read(const std::string &name, bool dryValue = false) {
    if (dryRun_) return dryValue;
#ifdef USE_GPIOD
    auto it = inputs_.find(name);
    if (it == inputs_.end() || !it->second) return false;
    return gpiod_line_get_value(it->second) > 0;
#else
    (void)name;
    return false;
#endif
  }

  void write(const std::string &name, bool value) {
    outputValues_[name] = value;
    if (dryRun_) return;
#ifdef USE_GPIOD
    auto it = outputs_.find(name);
    if (it != outputs_.end() && it->second) gpiod_line_set_value(it->second, value ? 1 : 0);
#else
    (void)name;
    (void)value;
#endif
  }

  void close() {
#ifdef USE_GPIOD
    for (auto &kv : inputs_) {
      if (kv.second) gpiod_line_release(kv.second);
    }
    for (auto &kv : outputs_) {
      if (kv.second) gpiod_line_release(kv.second);
    }
    if (chip_) gpiod_chip_close(chip_);
    chip_ = nullptr;
#endif
  }

 private:
  bool dryRun_ = false;
#ifdef USE_GPIOD
  gpiod_chip *chip_ = nullptr;
#endif
  std::map<std::string, void *> placeholders_;
#ifdef USE_GPIOD
  std::map<std::string, gpiod_line *> inputs_;
  std::map<std::string, gpiod_line *> outputs_;
#else
  std::map<std::string, void *> inputs_;
  std::map<std::string, void *> outputs_;
#endif
  std::map<std::string, bool> outputValues_;
};

void printHelp() {
  std::cout
      << "nexus_motor_plant_sim [options]\n"
      << "  --dry-run              run without GPIO\n"
      << "  --pin-test             pulse feedback outputs and print FPGA outputs\n"
      << "  --auto-run             dry-run input behaves like MOTOR_RUN_CMD=1\n"
      << "  --chip gpiochip0       gpiochip name\n"
      << "  --ads-serial PATH      FakeADS USB serial path\n"
      << "  --field-enable-bcm N   FIELD_ENABLE input BCM, default 26, -1 uses PWM compatibility\n"
      << "  --telemetry PATH       plant JSON path, default /tmp/nexus_motor_plant_state.json\n"
      << "  --cmd-fifo PATH        command FIFO path, default /tmp/nexus_motor_plant_cmd\n"
      << "  --fpga-state PATH      optional telemetry fallback, default /tmp/nexus_fpga_control_state.json\n"
      << "  --tick-ms N            control loop tick, default 5 ms\n"
      << "  --load PCT             simulated load percent, default 45\n";
}

Args parseArgs(int argc, char **argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) return "";
      return argv[++i];
    };
    if (a == "--dry-run") args.dryRun = true;
    else if (a == "--pin-test") args.pinTest = true;
    else if (a == "--auto-run") args.autoRun = true;
    else if (a == "--chip") args.chip = next();
    else if (a == "--ads-serial") args.adsSerial = next();
    else if (a == "--field-enable-bcm") args.fieldEnableBcm = std::stoi(next());
    else if (a == "--telemetry") args.telemetryPath = next();
    else if (a == "--cmd-fifo") args.commandFifo = next();
    else if (a == "--fpga-state") args.fpgaStatePath = next();
    else if (a == "--tick-ms") args.tickMs = std::max(1, std::stoi(next()));
    else if (a == "--load") args.loadPct = clampf(std::stof(next()), 0.0f, 120.0f);
    else if (a == "--help" || a == "-h") {
      printHelp();
      std::exit(0);
    }
  }
  return args;
}

ControlIn readControl(Gpio &gpio, const Args &args) {
  auto readFallback = [&]() -> FpgaControlFallback {
    FpgaControlFallback fb;
    if (args.fpgaStatePath.empty()) return fb;
    struct stat st {};
    if (::stat(args.fpgaStatePath.c_str(), &st) != 0) return fb;
    const time_t now = std::time(nullptr);
    if (now > 0 && st.st_mtime > 0 && now - st.st_mtime > 2) return fb;
    FILE *f = std::fopen(args.fpgaStatePath.c_str(), "r");
    if (!f) return fb;
    char buf[512] = {};
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    if (n == 0) return fb;
    auto numberAfter = [&](const char *key, float def) -> float {
      const char *pos = std::strstr(buf, key);
      if (!pos) return def;
      pos = std::strchr(pos, ':');
      if (!pos) return def;
      return std::strtof(pos + 1, nullptr);
    };
    fb.motorRun = numberAfter("\"run\"", 0.0f) > 0.5f;
    const bool relayFs = numberAfter("\"fs\"", 0.0f) > 0.5f;
    const float field = numberAfter("\"field\"", 0.0f);
    fb.faultOut = numberAfter("\"fault\"", 0.0f) > 0.5f;
    fb.fieldEnable = fb.motorRun && relayFs && !fb.faultOut;
    fb.fieldPwmSeen = field > 0.01f || fb.fieldEnable;
    fb.valid = true;
    return fb;
  };

  const FpgaControlFallback fallback = readFallback();
  ControlIn in;
  in.motorRunCmd = gpio.read("motor_run_cmd", args.autoRun);
  in.fieldPwmSeen = gpio.read("field_pwm_cmd", args.autoRun);
  in.fieldEnable = args.fieldEnableBcm >= 0 ? gpio.read("field_enable_cmd", args.autoRun) : in.fieldPwmSeen;
  in.syncPulse = gpio.read("sync_pulse", false);
  in.faultOut = gpio.read("fault_out", false);
  if (fallback.valid) {
    in.motorRunCmd = in.motorRunCmd || fallback.motorRun;
    in.fieldPwmSeen = in.fieldPwmSeen || fallback.fieldPwmSeen;
    in.fieldEnable = in.fieldEnable || fallback.fieldEnable;
    in.faultOut = in.faultOut || fallback.faultOut;
  }
  return in;
}

void updatePlant(Plant &p, const ControlIn &in, float dt) {
  p.fault = in.faultOut;
  if (!in.motorRunCmd && p.speedPct <= 0.1f) {
    p.forceStop = false;
  }
  const bool runCommand = in.motorRunCmd && !p.forceStop;
  if (p.fault) {
    p.state = PlantState::Faulted;
  } else if (!runCommand && p.speedPct <= 0.1f) {
    p.state = PlantState::Stopped;
  } else if (!runCommand) {
    p.state = PlantState::Coasting;
  } else if (p.speedPct < 8.0f) {
    p.state = PlantState::Energized;
  } else if (p.speedPct < 92.0f) {
    p.state = PlantState::Accelerating;
  } else if (!p.exciterOut.fieldApplied) {
    p.state = PlantState::NearSync;
  } else if (p.speedPct < 99.0f) {
    p.state = PlantState::FieldApplied;
  } else if (p.loadPct > 65.0f) {
    p.state = PlantState::Loaded;
  } else {
    p.state = PlantState::Synchronized;
  }

  if (p.scenario == PlantScenario::Pullout) {
    p.loadPct = approach(p.loadPct, 105.0f, 10.0f, dt);
  }

  const bool accelerating = runCommand && !p.fault;
  const float speedPu = clampf(p.speedPct / 100.0f, 0.0f, 1.0f);
  const bool fieldCommandedNow = in.fieldEnable && runCommand && !p.fault && p.speedPct > 75.0f;
  const float inductionTorque = accelerating ? clampf(2.1f - 1.25f * speedPu + 0.18f * std::sin(kPi * speedPu), 0.18f, 2.15f) : 0.0f;
  const float syncTorque = fieldCommandedNow ? clampf(p.fieldCurrentA / 4.2f, 0.0f, 1.4f) * clampf((p.speedPct - 88.0f) / 12.0f, 0.0f, 1.0f) : 0.0f;
  p.loadTorquePu = 0.18f + 0.62f * clampf(p.loadPct / 100.0f, 0.0f, 1.2f) + 0.08f * speedPu * speedPu;
  p.motorTorquePu = accelerating ? inductionTorque + syncTorque : 0.0f;
  if (p.scenario == PlantScenario::Pullout && p.fieldApplied) {
    p.motorTorquePu *= 0.72f;
  }
  const float accelPctPerSecond = (p.motorTorquePu - p.loadTorquePu) * 7.2f;
  const float coastRate = p.fault ? 24.0f : 7.5f + 2.0f * speedPu;
  if (accelerating) {
    p.speedPct += clampf(accelPctPerSecond, 0.8f, 13.0f) * dt;
    if (p.fieldApplied && p.speedPct > 96.0f) {
      p.speedPct = approach(p.speedPct, 100.0f, 2.4f + syncTorque, dt);
    }
  } else {
    p.speedPct = approach(p.speedPct, 0.0f, coastRate, dt);
  }
  p.speedPct = clampf(p.speedPct, 0.0f, 100.0f);
  p.slipHz = std::max(0.0f, 60.0f * (1.0f - p.speedPct / 100.0f));
  p.breakerClosed = runCommand && !p.fault;
  p.fieldApplied = in.fieldEnable && p.breakerClosed && p.speedPct > 75.0f && !p.fault;
  p.statorCurrentScale = p.breakerClosed ? clampf(1.9f - 0.9f * speedPu + 0.25f * p.loadTorquePu, 0.65f, 2.4f) : 0.0f;
  p.dischargeCurrentA = p.breakerClosed && p.speedPct < 92.0f ? 0.9f + (100.0f - p.speedPct) * 0.038f : 0.0f;
  if (p.scenario == PlantScenario::NoDischarge) p.dischargeCurrentA = 0.0f;

  ExciterBoardInput exciterIn;
  exciterIn.breakerClosed = p.breakerClosed;
  exciterIn.fieldEnableCmd = in.fieldEnable;
  exciterIn.controllerFault = p.fault;
  exciterIn.speedPct = p.speedPct;
  exciterIn.loadPct = p.loadPct;
  exciterIn.scenario = p.scenario;
  exciterIn.motorFieldCurrentA = p.fieldCurrentA;
  p.exciterOut = updateExciterBoard(p.exciter, exciterIn, dt);

  p.fieldApplied = p.exciterOut.fieldApplied;
  float fieldSourceV = p.exciterOut.fieldVoltageV;
  float effectiveResistance = p.exciter.fieldResistanceOhm;
  if (p.exciterOut.crowbarActive) {
    fieldSourceV = 0.0f;
    effectiveResistance += p.exciter.crowbarResistanceOhm;
  } else if (p.exciterOut.dischargeActive) {
    fieldSourceV = 0.0f;
    effectiveResistance += p.exciter.dischargeResistanceOhm;
  } else if (p.exciterOut.bleedActive) {
    fieldSourceV = 0.0f;
    effectiveResistance += p.exciter.bleedResistanceOhm;
  } else if (!p.fieldApplied) {
    fieldSourceV = 0.0f;
  }
  const float diDt = (fieldSourceV - p.fieldCurrentA * effectiveResistance) / std::max(0.1f, p.exciter.fieldInductanceH);
  p.fieldCurrentA = clampf(p.fieldCurrentA + diDt * dt, 0.0f, 20.0f);
  if (p.scenario == PlantScenario::NoField) p.fieldCurrentA = 0.0f;
  p.fieldVoltageV = p.fieldApplied ? fieldSourceV : (p.fieldCurrentA > 0.05f ? -p.fieldCurrentA * (effectiveResistance - p.exciter.fieldResistanceOhm) : 0.0f);
  if (p.exciterOut.dischargeActive) p.dischargeCurrentA = std::max(p.dischargeCurrentA, p.exciterOut.dischargeCurrentA);
  if (p.exciterOut.crowbarActive || p.exciterOut.bleedActive) p.dischargeCurrentA = std::max(p.dischargeCurrentA, p.exciterOut.dischargeCurrentA);
  p.exciter.fieldVoltageTransducerV = p.fieldVoltageV;
  p.exciter.fieldCurrentTransducerA = p.fieldCurrentA;
  p.exciter.dischargeCurrentTransducerA = p.dischargeCurrentA;
  p.pulloutRisk = clampf((p.loadPct - 80.0f) / 25.0f + (p.fieldApplied ? 0.0f : 0.25f), 0.0f, 1.0f);
  if (p.scenario == PlantScenario::ThermalTrip && p.speedPct > 20.0f) p.fault = true;
}

FeedbackOut feedbackFromPlant(const Plant &p) {
  FeedbackOut fb;
  const bool plantThermalTrip = p.scenario == PlantScenario::ThermalTrip && p.speedPct > 20.0f;
  fb.breakerClosed = p.breakerClosed;
  fb.speedOk = p.speedPct >= 95.0f;
  fb.fieldCurrent = p.fieldCurrentA >= 0.5f;
  fb.dischargeCurrent = p.dischargeCurrentA >= 0.2f;
  fb.thermalOk = !plantThermalTrip;
  fb.exciterReady = true;
  fb.loadReady = p.loadPct <= 100.0f;
  fb.emergencyOk = true;
  fb.plantFault = plantThermalTrip;
  return fb;
}

void writeFeedback(Gpio &gpio, const FeedbackOut &fb) {
  gpio.write("breaker_closed", fb.breakerClosed);
  gpio.write("speed_ok", fb.speedOk);
  gpio.write("field_current", fb.fieldCurrent);
  gpio.write("discharge_current", fb.dischargeCurrent);
  gpio.write("thermal_ok", fb.thermalOk);
  gpio.write("exciter_ready", fb.exciterReady);
  gpio.write("load_ready", fb.loadReady);
  gpio.write("emergency_ok", fb.emergencyOk);
  gpio.write("plant_fault", fb.plantFault);
}

std::string adsProfileForPlant(const Plant &p) {
  if (p.state == PlantState::Stopped || p.state == PlantState::Coasting) return "GRID_NORMAL";
  if (p.state == PlantState::Faulted || p.pulloutRisk > 0.95f) return "PULLOUT";
  if (p.state == PlantState::Energized || p.state == PlantState::Accelerating || p.state == PlantState::NearSync) return "START_PROFILE";
  return "GRID_NORMAL";
}

void maybeSendAdsProfile(SerialPort &ads, const Plant &p, std::string &lastProfile, bool dryRun) {
  const std::string profile = adsProfileForPlant(p);
  if (profile == lastProfile) return;
  lastProfile = profile;
  const std::string cmd = "PROFILE " + profile;
  if (dryRun || !ads.writeLine(cmd)) {
    std::cout << "ADS_CMD " << cmd << "\n";
  }
}

void maybeSendAdsPlantDrive(SerialPort &ads, const Plant &p, Clock::time_point &lastSend, bool dryRun) {
  const auto now = Clock::now();
  if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSend).count() < 200) return;
  lastSend = now;
  char cmd[160];
  std::snprintf(
      cmd,
      sizeof(cmd),
      "PLANT %.2f %.3f %.3f %.3f %.3f %.3f %.2f %.3f",
      p.speedPct,
      p.slipHz,
      p.statorCurrentScale,
      p.fieldCurrentA,
      p.fieldVoltageV,
      p.dischargeCurrentA,
      p.loadPct,
      p.pulloutRisk);
  if (dryRun || !ads.writeLine(cmd)) {
    std::cout << "ADS_CMD " << cmd << "\n";
  }
}

class CommandFifo {
 public:
  bool openFifo(const std::string &path) {
    path_ = path;
    if (path_.empty()) return false;
    ::mkfifo(path_.c_str(), 0666);
    ::chmod(path_.c_str(), 0666);
    fd_ = ::open(path_.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd_ < 0) {
      std::cerr << "command fifo open failed " << path_ << ": " << std::strerror(errno) << "\n";
      return false;
    }
    return true;
  }

  std::vector<std::string> readLines() {
    std::vector<std::string> lines;
    if (fd_ < 0) return lines;
    char buf[256];
    for (;;) {
      const ssize_t n = ::read(fd_, buf, sizeof(buf));
      if (n <= 0) break;
      pending_.append(buf, buf + n);
    }
    size_t pos = 0;
    while ((pos = pending_.find('\n')) != std::string::npos) {
      std::string line = pending_.substr(0, pos);
      pending_.erase(0, pos + 1);
      if (!line.empty()) lines.push_back(line);
    }
    return lines;
  }

  ~CommandFifo() {
    if (fd_ >= 0) ::close(fd_);
  }

 private:
  std::string path_;
  std::string pending_;
  int fd_ = -1;
};

void applyPlantCommand(Plant &plant, const std::string &line) {
  std::string cmd = line;
  while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == '\n' || cmd.back() == ' ')) cmd.pop_back();
  if (cmd.rfind("SCENARIO ", 0) == 0) {
    PlantScenario scenario;
    if (parseScenario(cmd.substr(9), scenario)) {
      plant.scenario = scenario;
      plant.forceStop = false;
      if (scenario != PlantScenario::Pullout) plant.loadPct = 45.0f;
      std::cout << "PLANT_CMD scenario=" << scenarioName(plant.scenario) << "\n";
    }
  } else if (cmd == "STOP") {
    plant.forceStop = true;
    plant.breakerClosed = false;
    plant.fieldApplied = false;
    std::cout << "PLANT_CMD stop\n";
  } else if (cmd == "RESET" || cmd == "SCENARIO NORMAL") {
    plant.scenario = PlantScenario::Normal;
    plant.forceStop = false;
    plant.fault = false;
    plant.loadPct = 45.0f;
    std::cout << "PLANT_CMD reset\n";
  }
}

void writeTelemetryJson(const std::string &path, const Plant &p, const ControlIn &in, const FeedbackOut &fb) {
  if (path.empty()) return;
  const std::string tmp = path + ".tmp";
  FILE *f = std::fopen(tmp.c_str(), "w");
  if (!f) return;
  std::fprintf(
      f,
      "{"
      "\"plant_state\":\"%s\","
      "\"plant_state_id\":%d,"
      "\"plant_scenario\":\"%s\","
      "\"speed_pct\":%.3f,"
      "\"slip_hz\":%.5f,"
      "\"load_pct\":%.5f,"
      "\"stator_current_scale\":%.5f,"
      "\"motor_torque_pu\":%.5f,"
      "\"load_torque_pu\":%.5f,"
      "\"field_current\":%.5f,"
      "\"field_voltage\":%.5f,"
      "\"discharge_current\":%.5f,"
      "\"pullout_risk\":%.5f,"
      "\"exciter_board_present\":1,"
      "\"field_enable\":%d,"
      "\"field_pwm_seen\":%d,"
      "\"breaker_closed\":%d,"
      "\"speed_ok\":%d,"
      "\"field_current_fb\":%d,"
      "\"discharge_current_fb\":%d,"
      "\"thermal_ok\":%d,"
      "\"plant_fault\":%d,"
      "\"rectifier_ready\":%d,"
      "\"rf3_enabled\":%d,"
      "\"pf_reg_disabled\":%d,"
      "\"scr_angle_deg\":%.3f,"
      "\"scr_firing_deg\":%.3f,"
      "\"scr_gate_width_deg\":%.3f,"
      "\"scr_active_pair\":%d,"
      "\"dc_bus_voltage\":%.5f,"
      "\"rectifier_avg_v\":%.5f,"
      "\"rectifier_ripple_v\":%.5f,"
      "\"rectifier_inst_v\":%.5f,"
      "\"rectifier_va\":%.5f,"
      "\"rectifier_vb\":%.5f,"
      "\"rectifier_vc\":%.5f,"
      "\"comm_overlap_deg\":%.5f,"
      "\"field_voltage_xdcr\":%.5f,"
      "\"field_current_xdcr\":%.5f,"
      "\"discharge_current_xdcr\":%.5f,"
      "\"pf_signal\":%.5f,"
      "\"bleed_active\":%d,"
      "\"discharge_active\":%d,"
      "\"crowbar_active\":%d,"
      "\"sk10\":%d,"
      "\"sk11\":%d,"
      "\"sk12\":%d,"
      "\"sk13\":%d,"
      "\"g1\":%d,\"g2\":%d,\"g3\":%d,\"g4\":%d,\"g5\":%d,\"g6\":%d,"
      "\"g1_exec\":%d,\"g2_exec\":%d,\"g3_exec\":%d,\"g4_exec\":%d,\"g5_exec\":%d,\"g6_exec\":%d,"
      "\"k1\":%d,\"k2\":%d,\"k3\":%d,\"k4\":%d,\"k5\":%d,\"k6\":%d,"
      "\"gate_fail_1\":%d,\"gate_fail_2\":%d,\"gate_fail_3\":%d,\"gate_fail_4\":%d,\"gate_fail_5\":%d,\"gate_fail_6\":%d,"
      "\"scr_open_1\":%d,\"scr_open_2\":%d,\"scr_open_3\":%d,\"scr_open_4\":%d,\"scr_open_5\":%d,\"scr_open_6\":%d,"
      "\"scr_short_1\":%d,\"scr_short_2\":%d,\"scr_short_3\":%d,\"scr_short_4\":%d,\"scr_short_5\":%d,\"scr_short_6\":%d"
      "}\n",
      stateName(p.state),
      stateId(p.state),
      scenarioName(p.scenario),
      p.speedPct,
      p.slipHz,
      p.loadPct,
      p.statorCurrentScale,
      p.motorTorquePu,
      p.loadTorquePu,
      p.fieldCurrentA,
      p.fieldVoltageV,
      p.dischargeCurrentA,
      p.pulloutRisk,
      in.fieldEnable ? 1 : 0,
      in.fieldPwmSeen ? 1 : 0,
      fb.breakerClosed ? 1 : 0,
      fb.speedOk ? 1 : 0,
      fb.fieldCurrent ? 1 : 0,
      fb.dischargeCurrent ? 1 : 0,
      fb.thermalOk ? 1 : 0,
      fb.plantFault ? 1 : 0,
      p.exciter.rectifierReady ? 1 : 0,
      p.exciter.rf3Enabled ? 1 : 0,
      p.exciter.powerFactorRegDisabled ? 1 : 0,
      p.exciter.electricalAngleDeg,
      p.exciter.firingAngleDeg,
      p.exciter.gatePulseWidthDeg,
      p.exciter.activePair,
      p.exciter.dcBusVoltageV,
      p.exciter.rectifierAverageV,
      p.exciter.rectifierRippleV,
      p.exciter.rectifierInstantV,
      p.exciter.rectifierPhaseVa,
      p.exciter.rectifierPhaseVb,
      p.exciter.rectifierPhaseVc,
      p.exciter.commutationOverlapDeg,
      p.exciter.fieldVoltageTransducerV,
      p.exciter.fieldCurrentTransducerA,
      p.exciter.dischargeCurrentTransducerA,
      p.exciter.powerFactorSignal,
      p.exciter.bleedActive ? 1 : 0,
      p.exciter.dischargeActive ? 1 : 0,
      p.exciter.crowbarActive ? 1 : 0,
      p.exciter.auxSk10 ? 1 : 0,
      p.exciter.auxSk11 ? 1 : 0,
      p.exciter.auxSk12 ? 1 : 0,
      p.exciter.auxSk13 ? 1 : 0,
      p.exciter.scrGate[0] ? 1 : 0,
      p.exciter.scrGate[1] ? 1 : 0,
      p.exciter.scrGate[2] ? 1 : 0,
      p.exciter.scrGate[3] ? 1 : 0,
      p.exciter.scrGate[4] ? 1 : 0,
      p.exciter.scrGate[5] ? 1 : 0,
      p.exciter.scrGateExec[0] ? 1 : 0,
      p.exciter.scrGateExec[1] ? 1 : 0,
      p.exciter.scrGateExec[2] ? 1 : 0,
      p.exciter.scrGateExec[3] ? 1 : 0,
      p.exciter.scrGateExec[4] ? 1 : 0,
      p.exciter.scrGateExec[5] ? 1 : 0,
      p.exciter.scrConducting[0] ? 1 : 0,
      p.exciter.scrConducting[1] ? 1 : 0,
      p.exciter.scrConducting[2] ? 1 : 0,
      p.exciter.scrConducting[3] ? 1 : 0,
      p.exciter.scrConducting[4] ? 1 : 0,
      p.exciter.scrConducting[5] ? 1 : 0,
      p.exciter.gateFailed[0] ? 1 : 0,
      p.exciter.gateFailed[1] ? 1 : 0,
      p.exciter.gateFailed[2] ? 1 : 0,
      p.exciter.gateFailed[3] ? 1 : 0,
      p.exciter.gateFailed[4] ? 1 : 0,
      p.exciter.gateFailed[5] ? 1 : 0,
      p.exciter.scrOpen[0] ? 1 : 0,
      p.exciter.scrOpen[1] ? 1 : 0,
      p.exciter.scrOpen[2] ? 1 : 0,
      p.exciter.scrOpen[3] ? 1 : 0,
      p.exciter.scrOpen[4] ? 1 : 0,
      p.exciter.scrOpen[5] ? 1 : 0,
      p.exciter.scrShort[0] ? 1 : 0,
      p.exciter.scrShort[1] ? 1 : 0,
      p.exciter.scrShort[2] ? 1 : 0,
      p.exciter.scrShort[3] ? 1 : 0,
      p.exciter.scrShort[4] ? 1 : 0,
      p.exciter.scrShort[5] ? 1 : 0);
  std::fclose(f);
  ::rename(tmp.c_str(), path.c_str());
}

void printStatus(const Plant &p, const ControlIn &in, const FeedbackOut &fb) {
  std::cout << "PLANT"
            << " state=" << stateName(p.state)
            << " scenario=" << scenarioName(p.scenario)
            << " motorcmd=" << (in.motorRunCmd ? 1 : 0)
            << " fielden=" << (in.fieldEnable ? 1 : 0)
            << " fieldpwm=" << (in.fieldPwmSeen ? 1 : 0)
            << " faultin=" << (in.faultOut ? 1 : 0)
            << " breaker=" << (fb.breakerClosed ? 1 : 0)
            << " speedok=" << (fb.speedOk ? 1 : 0)
            << " fieldfb=" << (fb.fieldCurrent ? 1 : 0)
            << " discfb=" << (fb.dischargeCurrent ? 1 : 0)
            << " speed=" << p.speedPct
            << " slip=" << p.slipHz
            << " load=" << p.loadPct
            << " fielda=" << p.fieldCurrentA
            << " fieldv=" << p.fieldVoltageV
            << " disca=" << p.dischargeCurrentA
            << " rf3=" << (p.exciter.rf3Enabled ? 1 : 0)
            << " firedeg=" << p.exciter.firingAngleDeg
            << " g=" << (p.exciter.scrGate[0] ? 1 : 0)
            << (p.exciter.scrGate[1] ? 1 : 0)
            << (p.exciter.scrGate[2] ? 1 : 0)
            << (p.exciter.scrGate[3] ? 1 : 0)
            << (p.exciter.scrGate[4] ? 1 : 0)
            << (p.exciter.scrGate[5] ? 1 : 0)
            << " k=" << (p.exciter.scrConducting[0] ? 1 : 0)
            << (p.exciter.scrConducting[1] ? 1 : 0)
            << (p.exciter.scrConducting[2] ? 1 : 0)
            << (p.exciter.scrConducting[3] ? 1 : 0)
            << (p.exciter.scrConducting[4] ? 1 : 0)
            << (p.exciter.scrConducting[5] ? 1 : 0)
            << " crowbar=" << (p.exciter.crowbarActive ? 1 : 0)
            << " risk=" << p.pulloutRisk
            << "\n";
}

void runPinTest(Gpio &gpio, const Args &args) {
  const std::vector<std::string> outputs = {
      "breaker_closed", "speed_ok", "field_current", "discharge_current", "thermal_ok",
      "exciter_ready", "load_ready", "emergency_ok", "plant_fault"};

  std::cout << "PIN_TEST start. Watch FakeFPGA telemetry pfb_* values.\n";
  for (const auto &name : outputs) gpio.write(name, false);
  gpio.write("thermal_ok", true);
  gpio.write("exciter_ready", true);
  gpio.write("load_ready", true);
  gpio.write("emergency_ok", true);

  auto printInputs = [&]() {
    ControlIn in = readControl(gpio, args);
    std::cout << "FPGA_OUT motor_run=" << (in.motorRunCmd ? 1 : 0)
              << " field_enable=" << (in.fieldEnable ? 1 : 0)
              << " field_pwm=" << (in.fieldPwmSeen ? 1 : 0)
              << " sync=" << (in.syncPulse ? 1 : 0)
              << " fault=" << (in.faultOut ? 1 : 0)
              << "\n";
  };

  printInputs();
  for (const auto &name : outputs) {
    std::cout << "PIN_TEST pulse " << name << "=1\n";
    gpio.write(name, true);
    for (int i = 0; i < 10 && g_running; ++i) {
      printInputs();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    gpio.write(name, false);
    if (name == "thermal_ok" || name == "exciter_ready" || name == "load_ready" || name == "emergency_ok") {
      gpio.write(name, true);
    }
  }
  std::cout << "PIN_TEST done\n";
}

}  // namespace

int main(int argc, char **argv) {
  std::cout.setf(std::ios::unitbuf);
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  const Args args = parseArgs(argc, argv);
  InputPins inPins;
  OutputPins outPins;

  Gpio gpio(args.dryRun);
  if (!gpio.openChip(args.chip)) return 2;
  if (!gpio.addInput("motor_run_cmd", inPins.motorRunCmd)) return 2;
  if (!gpio.addInput("field_pwm_cmd", inPins.fieldPwmCmd)) return 2;
  if (args.fieldEnableBcm >= 0) {
    inPins.fieldEnableCmd = args.fieldEnableBcm;
    if (!gpio.addInput("field_enable_cmd", inPins.fieldEnableCmd)) return 2;
  }
  if (!gpio.addInput("sync_pulse", inPins.syncPulse)) return 2;
  if (!gpio.addInput("fault_out", inPins.faultOut)) return 2;

  if (!gpio.addOutput("breaker_closed", outPins.breakerClosed, false)) return 2;
  if (!gpio.addOutput("speed_ok", outPins.speedOk, false)) return 2;
  if (!gpio.addOutput("field_current", outPins.fieldCurrent, false)) return 2;
  if (!gpio.addOutput("discharge_current", outPins.dischargeCurrent, false)) return 2;
  if (!gpio.addOutput("thermal_ok", outPins.thermalOk, true)) return 2;
  if (!gpio.addOutput("exciter_ready", outPins.exciterReady, true)) return 2;
  if (!gpio.addOutput("load_ready", outPins.loadReady, true)) return 2;
  if (!gpio.addOutput("emergency_ok", outPins.emergencyOk, true)) return 2;
  if (!gpio.addOutput("plant_fault", outPins.plantFault, false)) return 2;

  if (args.pinTest) {
    runPinTest(gpio, args);
    return 0;
  }

  SerialPort ads;
  if (!args.adsSerial.empty()) {
    ads.openPort(args.adsSerial);
    ads.writeLine("START");
  }

  Plant plant;
  plant.loadPct = args.loadPct;
  CommandFifo fifo;
  fifo.openFifo(args.commandFifo);
  std::string lastProfile;
  auto lastAdsPlantDrive = Clock::now() - std::chrono::seconds(1);
  auto last = Clock::now();
  auto lastPrint = last;

  std::cout << "Nexus motor plant simulator started"
            << " dry_run=" << (args.dryRun ? 1 : 0)
            << " tick_ms=" << args.tickMs
            << " ads_serial=" << (args.adsSerial.empty() ? "none" : args.adsSerial)
            << "\n";

  while (g_running) {
    const auto now = Clock::now();
    const float dt = std::chrono::duration<float>(now - last).count();
    last = now;

    for (const auto &command : fifo.readLines()) {
      applyPlantCommand(plant, command);
    }

    const ControlIn in = readControl(gpio, args);
    updatePlant(plant, in, dt);
    const FeedbackOut fb = feedbackFromPlant(plant);
    writeFeedback(gpio, fb);
    writeTelemetryJson(args.telemetryPath, plant, in, fb);
    maybeSendAdsProfile(ads, plant, lastProfile, args.dryRun);
    maybeSendAdsPlantDrive(ads, plant, lastAdsPlantDrive, args.dryRun);

    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPrint).count() >= 500) {
      printStatus(plant, in, fb);
      lastPrint = now;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(args.tickMs));
  }

  FeedbackOut safe;
  safe.thermalOk = false;
  safe.exciterReady = false;
  safe.loadReady = false;
  safe.emergencyOk = false;
  safe.plantFault = true;
  writeFeedback(gpio, safe);
  if (!args.adsSerial.empty()) ads.writeLine("PROFILE NO_SIGNAL");
  std::cout << "Nexus motor plant simulator stopped\n";
  return 0;
}
