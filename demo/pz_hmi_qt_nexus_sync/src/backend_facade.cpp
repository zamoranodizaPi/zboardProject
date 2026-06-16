#include "backend_facade.h"

BackendFacade::BackendFacade(TelemetryModel *model, DataProvider *provider, QObject *parent)
    : QObject(parent), m_provider(provider) {
    connect(provider, &DataProvider::telemetryReady, model, &TelemetryModel::applyTelemetry);
    connect(provider, &DataProvider::providerStatusChanged, model, &TelemetryModel::setProviderStatus);
}

void BackendFacade::startMotor() { m_provider->sendCommand("START"); }
void BackendFacade::stopMotor() { m_provider->sendCommand("STOP"); }
void BackendFacade::reset() { m_provider->sendCommand("RESET"); }
void BackendFacade::ack() { m_provider->sendCommand("ACK"); }
