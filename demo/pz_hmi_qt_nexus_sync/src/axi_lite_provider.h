#pragma once

#include "data_provider.h"

#include <QTimer>

class AxiLiteProvider : public DataProvider {
    Q_OBJECT

public:
    explicit AxiLiteProvider(quintptr baseAddress, QObject *parent = nullptr);

public slots:
    void start() override;
    void stop() override;
    void sendCommand(const QString &command) override;

private slots:
    void poll();

private:
    quintptr m_baseAddress = 0;
    QTimer m_pollTimer;
};
