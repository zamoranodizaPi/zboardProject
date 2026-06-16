#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class TelemetryModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString motorState READ motorState NOTIFY changed)
    Q_PROPERTY(QString syncState READ syncState NOTIFY changed)
    Q_PROPERTY(QString alarmText READ alarmText NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(double frequency READ frequency NOTIFY changed)
    Q_PROPERTY(double voltage READ voltage NOTIFY changed)
    Q_PROPERTY(double current READ current NOTIFY changed)
    Q_PROPERTY(double powerFactor READ powerFactor NOTIFY changed)
    Q_PROPERTY(double loadAngle READ loadAngle NOTIFY changed)
    Q_PROPERTY(double speedPct READ speedPct NOTIFY changed)
    Q_PROPERTY(double slipHz READ slipHz NOTIFY changed)
    Q_PROPERTY(double fieldVoltage READ fieldVoltage NOTIFY changed)
    Q_PROPERTY(double fieldCurrent READ fieldCurrent NOTIFY changed)
    Q_PROPERTY(double dischargeCurrent READ dischargeCurrent NOTIFY changed)
    Q_PROPERTY(bool fs READ fs NOTIFY changed)
    Q_PROPERTY(bool fal READ fal NOTIFY changed)
    Q_PROPERTY(bool ok56k READ ok56k NOTIFY changed)
    Q_PROPERTY(bool fwt READ fwt NOTIFY changed)
    Q_PROPERTY(bool dst READ dst NOTIFY changed)
    Q_PROPERTY(QVariantList trend READ trend NOTIFY trendChanged)

public:
    explicit TelemetryModel(QObject *parent = nullptr);

    QString motorState() const { return m_motorState; }
    QString syncState() const { return m_syncState; }
    QString alarmText() const { return m_alarmText; }
    bool connected() const { return m_connected; }
    double frequency() const { return m_frequency; }
    double voltage() const { return m_voltage; }
    double current() const { return m_current; }
    double powerFactor() const { return m_powerFactor; }
    double loadAngle() const { return m_loadAngle; }
    double speedPct() const { return m_speedPct; }
    double slipHz() const { return m_slipHz; }
    double fieldVoltage() const { return m_fieldVoltage; }
    double fieldCurrent() const { return m_fieldCurrent; }
    double dischargeCurrent() const { return m_dischargeCurrent; }
    bool fs() const { return m_fs; }
    bool fal() const { return m_fal; }
    bool ok56k() const { return m_ok56k; }
    bool fwt() const { return m_fwt; }
    bool dst() const { return m_dst; }
    QVariantList trend() const { return m_trend; }

public slots:
    void applyTelemetry(const QVariantMap &telemetry);
    void setProviderStatus(bool connected, const QString &message);

signals:
    void changed();
    void trendChanged();

private:
    static double number(const QVariantMap &m, const char *key, double fallback = 0.0);
    static bool flag(const QVariantMap &m, const char *key);
    static QString stateName(int state);

    QString m_motorState = "INIT";
    QString m_syncState = "WAIT";
    QString m_alarmText = "No alarm";
    bool m_connected = false;
    double m_frequency = 0.0;
    double m_voltage = 0.0;
    double m_current = 0.0;
    double m_powerFactor = 0.0;
    double m_loadAngle = 0.0;
    double m_speedPct = 0.0;
    double m_slipHz = 0.0;
    double m_fieldVoltage = 0.0;
    double m_fieldCurrent = 0.0;
    double m_dischargeCurrent = 0.0;
    bool m_fs = false;
    bool m_fal = false;
    bool m_ok56k = false;
    bool m_fwt = false;
    bool m_dst = false;
    QVariantList m_trend;
};
