#include "telemetry_model.h"

#include <QDateTime>
#include <QtMath>

TelemetryModel::TelemetryModel(QObject *parent) : QObject(parent) {}

double TelemetryModel::number(const QVariantMap &m, const char *key, double fallback) {
    const auto it = m.constFind(QString::fromLatin1(key));
    if (it == m.constEnd()) {
        return fallback;
    }
    bool ok = false;
    const double v = it->toDouble(&ok);
    return ok ? v : fallback;
}

bool TelemetryModel::flag(const QVariantMap &m, const char *key) {
    return qAbs(number(m, key, 0.0)) > 0.5;
}

QString TelemetryModel::stateName(int state) {
    switch (state) {
    case 0: return "IDLE";
    case 1: return "READY";
    case 2: return "STARTING";
    case 3: return "ACCELERATION";
    case 4: return "FIELD APPLY";
    case 5: return "SYNC VERIFY";
    case 6: return "RUNNING";
    case 7: return "FAULT";
    case 8: return "LOCKOUT";
    default: return "UNKNOWN";
    }
}

void TelemetryModel::applyTelemetry(const QVariantMap &t) {
    const int ctrl = static_cast<int>(number(t, "ctrl", number(t, "ctrl_state", 0)));
    m_motorState = t.value("plant_state", stateName(ctrl)).toString();
    if (m_motorState.isEmpty()) {
        m_motorState = stateName(ctrl);
    }

    m_frequency = number(t, "f", number(t, "frequency", 0.0));
    const double va = number(t, "va", number(t, "v_a", 0.0));
    const double vb = number(t, "vb", number(t, "v_b", va));
    const double vc = number(t, "vc", number(t, "v_c", va));
    m_voltage = (qAbs(va) + qAbs(vb) + qAbs(vc)) / 3.0;

    const double ia = number(t, "ia", number(t, "i_a", 0.0));
    const double ib = number(t, "ib", number(t, "i_b", ia));
    const double ic = number(t, "ic", number(t, "i_c", ia));
    m_current = (qAbs(ia) + qAbs(ib) + qAbs(ic)) / 3.0;

    m_powerFactor = number(t, "pf", 0.0);
    m_loadAngle = number(t, "angle", number(t, "load_angle", 0.0));
    m_speedPct = number(t, "speed_pct", 0.0);
    m_slipHz = number(t, "slip_hz", 0.0);
    m_fieldVoltage = number(t, "field_voltage", number(t, "fieldv", 0.0));
    m_fieldCurrent = number(t, "field_current", number(t, "fielda", 0.0));
    m_dischargeCurrent = number(t, "discharge_current", 0.0);
    m_fs = flag(t, "fs") || flag(t, "field_enable");
    m_fal = flag(t, "fal") || flag(t, "fault");
    m_ok56k = flag(t, "ok56k") || ctrl >= 1;
    m_fwt = flag(t, "fwt");
    m_dst = flag(t, "dst");
    m_syncState = flag(t, "sync") || ctrl == 6 ? "SYNCHRONIZED" : "WAIT";
    m_alarmText = m_fal ? "Fault active" : "No alarm";

    QVariantMap point;
    point["ts"] = QDateTime::currentMSecsSinceEpoch();
    point["voltage"] = m_voltage;
    point["current"] = m_current;
    point["frequency"] = m_frequency;
    point["field"] = m_fieldCurrent;
    m_trend.append(point);
    while (m_trend.size() > 120) {
        m_trend.removeFirst();
    }

    emit changed();
    emit trendChanged();
}

void TelemetryModel::setProviderStatus(bool connected, const QString &) {
    if (m_connected == connected) {
        return;
    }
    m_connected = connected;
    emit changed();
}
