#pragma once

#include "data_provider.h"

#include <QElapsedTimer>
#include <QTimer>

class DemoDataProvider : public DataProvider {
    Q_OBJECT

public:
    explicit DemoDataProvider(QObject *parent = nullptr);

public slots:
    void start() override;
    void stop() override;
    void sendCommand(const QString &command) override;

private slots:
    void tick();

private:
    QTimer m_timer;
    QElapsedTimer m_clock;
    int m_state = 1;
    bool m_running = false;
    double m_speed = 0.0;
};
