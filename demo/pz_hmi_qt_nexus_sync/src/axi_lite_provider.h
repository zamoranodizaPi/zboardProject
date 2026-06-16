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
    bool openMap();
    void closeMap();
    quint32 readReg(quintptr offset) const;
    void writeReg(quintptr offset, quint32 value);
    static qint16 high16(quint32 value);
    static qint16 low16(quint32 value);
    static double scaleVoltage(qint16 raw);
    static double scaleCurrent(qint16 raw);

    quintptr m_baseAddress = 0;
    int m_fd = -1;
    volatile quint32 *m_regs = nullptr;
    bool m_mapped = false;
    QTimer m_pollTimer;
};
