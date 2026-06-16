#include "axi_lite_provider.h"

#include <QDebug>
#include <QtGlobal>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {
constexpr quint32 REG_ID = 0x00;
constexpr quint32 REG_CONTROL = 0x08;
constexpr quint32 REG_STATUS = 0x0C;
constexpr quint32 REG_FRAMES_GOOD = 0x10;
constexpr quint32 REG_FRAMES_BAD = 0x14;
constexpr quint32 REG_FREQ_MHZ = 0x18;
constexpr quint32 REG_ADS_STATUS = 0x1C;
constexpr quint32 REG_CH0_CH1 = 0x20;
constexpr quint32 REG_CH2_CH3 = 0x24;
constexpr quint32 REG_CH4_CH5 = 0x28;
constexpr quint32 REG_CH6_CH7 = 0x2C;
constexpr quint32 REG_FIELD = 0x30;
constexpr quint32 NEXUS_ID = 0x4E53594E;
constexpr size_t MAP_SIZE = 4096;

constexpr double RAW_PEAK_AT_NOMINAL_V = 0.80 * 32767.0;
constexpr double RAW_PEAK_AT_NOMINAL_I = 0.55 * 32767.0;
constexpr double VOLTAGE_RMS_NOMINAL = 120.0;
constexpr double CURRENT_RMS_NOMINAL = 5.0;
}

AxiLiteProvider::AxiLiteProvider(quintptr baseAddress, QObject *parent)
    : DataProvider(parent), m_baseAddress(baseAddress) {
    m_pollTimer.setInterval(100);
    connect(&m_pollTimer, &QTimer::timeout, this, &AxiLiteProvider::poll);
}

void AxiLiteProvider::start() {
    if (!openMap()) {
        emit providerStatusChanged(false, "AXI mmap failed");
        return;
    }
    const quint32 id = readReg(REG_ID);
    if (id != NEXUS_ID) {
        qWarning() << "AXI provider: unexpected ID 0x" + QString::number(id, 16)
                   << "at 0x" + QString::number(m_baseAddress, 16);
        emit providerStatusChanged(false, "AXI ID mismatch");
        return;
    }
    qInfo() << "AXI provider: mapped Nexus Sync registers at 0x" + QString::number(m_baseAddress, 16);
    m_pollTimer.start();
    emit providerStatusChanged(true, "AXI provider connected");
}

void AxiLiteProvider::stop() {
    m_pollTimer.stop();
    closeMap();
    emit providerStatusChanged(false, "stopped");
}

void AxiLiteProvider::sendCommand(const QString &command) {
    if (!m_mapped) {
        emit commandResult(command, false, "AXI not mapped");
        return;
    }

    quint32 bit = 0;
    if (command == "START") {
        bit = 1u << 0;
    } else if (command == "STOP") {
        bit = 1u << 1;
    } else if (command == "RESET" || command == "ACK") {
        bit = 1u << 2;
    } else {
        emit commandResult(command, false, "unknown command");
        return;
    }

    writeReg(REG_CONTROL, bit);
    writeReg(REG_CONTROL, 0);
    emit commandResult(command, true, "AXI pulse sent");
}

void AxiLiteProvider::poll() {
    if (!m_mapped) {
        emit providerStatusChanged(false, "AXI not mapped");
        return;
    }

    const quint32 status = readReg(REG_STATUS);
    const quint32 ch01 = readReg(REG_CH0_CH1);
    const quint32 ch23 = readReg(REG_CH2_CH3);
    const quint32 ch45 = readReg(REG_CH4_CH5);
    const quint32 ch67 = readReg(REG_CH6_CH7);
    const quint32 field = readReg(REG_FIELD);

    const qint16 ch0 = high16(ch01);
    const qint16 ch1 = low16(ch01);
    const qint16 ch2 = high16(ch23);
    const qint16 ch3 = low16(ch23);
    const qint16 ch4 = high16(ch45);
    const qint16 ch5 = low16(ch45);
    const qint16 ch6 = high16(ch67);
    const qint16 ch7 = low16(ch67);

    QVariantMap t;
    t["ctrl"] = static_cast<int>(status & 0x0F);
    t["freq_locked"] = (status >> 4) & 1u;
    t["fault"] = (status >> 12) & 1u;
    t["run"] = (status >> 13) & 1u;
    t["field_enable"] = (status >> 14) & 1u;
    t["frame_valid"] = (status >> 15) & 1u;
    t["frame_bad"] = (status >> 16) & 1u;
    t["frames_good"] = readReg(REG_FRAMES_GOOD);
    t["frames_bad"] = readReg(REG_FRAMES_BAD);
    t["f"] = readReg(REG_FREQ_MHZ) / 1000.0;
    t["ads_status"] = readReg(REG_ADS_STATUS);
    t["va"] = scaleVoltage(ch0);
    t["vb"] = scaleVoltage(ch1);
    t["vc"] = scaleVoltage(ch2);
    t["v_a"] = t["va"];
    t["v_b"] = t["vb"];
    t["v_c"] = t["vc"];
    t["field_voltage"] = (static_cast<double>(ch3) / 32767.0) * 150.0;
    t["ia"] = scaleCurrent(ch4);
    t["ib"] = scaleCurrent(ch5);
    t["ic"] = scaleCurrent(ch6);
    t["i_a"] = t["ia"];
    t["i_b"] = t["ib"];
    t["i_c"] = t["ic"];
    t["discharge_current"] = (qAbs(ch7) / 32767.0) * 10.0;
    t["field_current"] = (field & 0x03FFu) * (5.5 / 1023.0);
    t["fs"] = t["field_enable"];
    t["fal"] = t["fault"];
    t["ok56k"] = 1;
    t["sync"] = ((status & 0x0F) == 6) ? 1 : 0;
    emit telemetryReady(t);
}

bool AxiLiteProvider::openMap() {
    if (m_mapped) {
        return true;
    }
    if (m_baseAddress == 0) {
        qWarning() << "AXI provider: base address is zero";
        return false;
    }
    m_fd = ::open("/dev/mem", O_RDWR | O_SYNC);
    if (m_fd < 0) {
        qWarning() << "AXI provider: cannot open /dev/mem";
        return false;
    }

    void *mapped = ::mmap(nullptr, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, static_cast<off_t>(m_baseAddress));
    if (mapped == MAP_FAILED) {
        qWarning() << "AXI provider: mmap failed at 0x" + QString::number(m_baseAddress, 16);
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    m_regs = static_cast<volatile quint32 *>(mapped);
    m_mapped = true;
    return true;
}

void AxiLiteProvider::closeMap() {
    if (m_mapped) {
        ::munmap(const_cast<quint32 *>(m_regs), MAP_SIZE);
        m_regs = nullptr;
        m_mapped = false;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

quint32 AxiLiteProvider::readReg(quintptr offset) const {
    return m_regs[offset / sizeof(quint32)];
}

void AxiLiteProvider::writeReg(quintptr offset, quint32 value) {
    m_regs[offset / sizeof(quint32)] = value;
}

qint16 AxiLiteProvider::high16(quint32 value) {
    return static_cast<qint16>((value >> 16) & 0xFFFFu);
}

qint16 AxiLiteProvider::low16(quint32 value) {
    return static_cast<qint16>(value & 0xFFFFu);
}

double AxiLiteProvider::scaleVoltage(qint16 raw) {
    return (static_cast<double>(qAbs(raw)) / RAW_PEAK_AT_NOMINAL_V) * VOLTAGE_RMS_NOMINAL;
}

double AxiLiteProvider::scaleCurrent(qint16 raw) {
    return (static_cast<double>(qAbs(raw)) / RAW_PEAK_AT_NOMINAL_I) * CURRENT_RMS_NOMINAL;
}
