#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class ExciterInterface : public QObject {
    Q_OBJECT

public:
    explicit ExciterInterface(QObject *parent = nullptr) : QObject(parent) {}
    ~ExciterInterface() override = default;

public slots:
    virtual void requestScenario(const QString &scenario) = 0;
    virtual void requestReset() = 0;

signals:
    void exciterTelemetryReady(const QVariantMap &telemetry);
    void exciterStatusChanged(bool connected, const QString &message);
};
