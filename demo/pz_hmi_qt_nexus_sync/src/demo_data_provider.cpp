#include "demo_data_provider.h"

#include <QtMath>

DemoDataProvider::DemoDataProvider(QObject *parent) : DataProvider(parent) {
    m_timer.setInterval(100);
    connect(&m_timer, &QTimer::timeout, this, &DemoDataProvider::tick);
}

void DemoDataProvider::start() {
    m_clock.start();
    m_timer.start();
    emit providerStatusChanged(true, "demo provider");
}

void DemoDataProvider::stop() {
    m_timer.stop();
    emit providerStatusChanged(false, "stopped");
}

void DemoDataProvider::sendCommand(const QString &command) {
    if (command == "START") {
        m_running = true;
        m_state = 2;
    } else if (command == "STOP") {
        m_running = false;
        m_state = 1;
    } else if (command == "RESET" || command == "ACK") {
        m_state = 1;
    }
    emit commandResult(command, true, "accepted");
}

void DemoDataProvider::tick() {
    const double t = m_clock.elapsed() / 1000.0;
    if (m_running) {
        m_speed = qMin(100.0, m_speed + 0.45);
        if (m_speed < 35.0) m_state = 2;
        else if (m_speed < 90.0) m_state = 3;
        else if (m_speed < 98.0) m_state = 4;
        else m_state = 6;
    } else {
        m_speed = qMax(0.0, m_speed - 0.7);
        m_state = m_speed > 1.0 ? 3 : 1;
    }

    QVariantMap m;
    m["ctrl"] = m_state;
    m["f"] = 60.0 + 0.015 * qSin(t * 0.9);
    m["va"] = 120.0;
    m["vb"] = 120.0;
    m["vc"] = 120.0;
    m["ia"] = m_running ? 5.0 + 2.0 * qMax(0.0, 1.0 - m_speed / 100.0) : 0.3;
    m["ib"] = m["ia"];
    m["ic"] = m["ia"];
    m["pf"] = m_state == 6 ? 0.97 : 0.62 + 0.3 * (m_speed / 100.0);
    m["angle"] = m_state == 6 ? 14.0 + 2.0 * qSin(t * 0.5) : 35.0;
    m["speed_pct"] = m_speed;
    m["slip_hz"] = qMax(0.0, 60.0 * (1.0 - m_speed / 100.0));
    m["field_voltage"] = m_state >= 4 ? 110.0 : 0.0;
    m["field_current"] = m_state >= 4 ? 4.8 : 0.0;
    m["discharge_current"] = m_state == 2 || m_state == 3 ? 1.4 : 0.0;
    m["field_enable"] = m_state >= 4 ? 1 : 0;
    m["fs"] = m_state >= 4 ? 1 : 0;
    m["fal"] = 0;
    m["fault"] = 0;
    m["ok56k"] = 1;
    m["fwt"] = m_state >= 2 && m_state <= 3 ? 1 : 0;
    m["dst"] = m_state == 1 ? 1 : 0;
    m["sync"] = m_state == 6 ? 1 : 0;
    emit telemetryReady(m);
}
