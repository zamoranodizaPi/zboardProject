#pragma once

#include "data_provider.h"
#include "telemetry_model.h"

#include <QObject>
#include <QString>

class BackendFacade : public QObject {
    Q_OBJECT

public:
    BackendFacade(TelemetryModel *model, DataProvider *provider, QObject *parent = nullptr);

    Q_INVOKABLE void startMotor();
    Q_INVOKABLE void stopMotor();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void ack();

private:
    DataProvider *m_provider = nullptr;
};
