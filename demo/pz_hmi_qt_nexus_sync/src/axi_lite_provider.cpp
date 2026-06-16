#include "axi_lite_provider.h"

AxiLiteProvider::AxiLiteProvider(quintptr baseAddress, QObject *parent)
    : DataProvider(parent), m_baseAddress(baseAddress) {
    m_pollTimer.setInterval(100);
    connect(&m_pollTimer, &QTimer::timeout, this, &AxiLiteProvider::poll);
}

void AxiLiteProvider::start() {
    m_pollTimer.start();
    emit providerStatusChanged(false, "AXI provider stub: mmap not enabled yet");
}

void AxiLiteProvider::stop() {
    m_pollTimer.stop();
    emit providerStatusChanged(false, "stopped");
}

void AxiLiteProvider::sendCommand(const QString &command) {
    emit commandResult(command, false, "AXI command path not connected yet");
}

void AxiLiteProvider::poll() {
    Q_UNUSED(m_baseAddress)
    QVariantMap t;
    t["ctrl"] = 0;
    t["fault"] = 0;
    t["frame_valid"] = 0;
    emit telemetryReady(t);
}
