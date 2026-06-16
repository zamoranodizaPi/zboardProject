#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class DataProvider : public QObject {
    Q_OBJECT

public:
    explicit DataProvider(QObject *parent = nullptr) : QObject(parent) {}
    ~DataProvider() override = default;

public slots:
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void sendCommand(const QString &command) = 0;

signals:
    void telemetryReady(const QVariantMap &telemetry);
    void providerStatusChanged(bool connected, const QString &message);
    void commandResult(const QString &command, bool accepted, const QString &message);
};
