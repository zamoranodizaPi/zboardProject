#include "backend_facade.h"
#include "axi_lite_provider.h"
#include "demo_data_provider.h"
#include "telemetry_model.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDebug>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

namespace {

QLabel *makeLabel(const QString &text, const QString &objectName = {}) {
    auto *label = new QLabel(text);
    if (!objectName.isEmpty()) {
        label->setObjectName(objectName);
    }
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return label;
}

QLabel *makeValue(QGridLayout *layout, int row, int labelColumn, const QString &labelText) {
    auto *label = makeLabel(labelText, "metricLabel");
    auto *value = makeLabel("--", "metricValue");
    layout->addWidget(label, row, labelColumn);
    layout->addWidget(value, row, labelColumn + 1);
    return value;
}

QPushButton *makeCommandButton(const QString &text, const QString &objectName) {
    auto *button = new QPushButton(text);
    button->setObjectName(objectName);
    button->setMinimumHeight(56);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

QString yesNo(bool value) {
    return value ? "ON" : "OFF";
}

class LiteHmi final : public QWidget {
    Q_OBJECT

public:
    LiteHmi(TelemetryModel *model, BackendFacade *backend) : m_model(model), m_backend(backend) {
        qInfo() << "Nexus HMI lite: constructing widgets";
        setWindowTitle("Nexus Sync Embedded Lite");
        setObjectName("root");
        setMinimumSize(1024, 600);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(18, 14, 18, 14);
        root->setSpacing(12);

        auto *header = new QHBoxLayout();
        auto *titleBlock = new QVBoxLayout();
        auto *title = makeLabel("Nexus Sync", "title");
        auto *subtitle = makeLabel("Embedded HMI Lite", "subtitle");
        titleBlock->addWidget(title);
        titleBlock->addWidget(subtitle);
        header->addLayout(titleBlock, 1);
        m_clock = makeLabel("--:--:--", "clock");
        header->addWidget(m_clock);
        root->addLayout(header);

        auto *top = new QHBoxLayout();
        top->setSpacing(12);
        top->addWidget(createStatePanel(), 1);
        top->addWidget(createMeasurementsPanel(), 2);
        top->addWidget(createSignalsPanel(), 1);
        root->addLayout(top, 1);

        auto *bottom = new QHBoxLayout();
        bottom->setSpacing(12);
        bottom->addWidget(createCommandPanel(), 1);
        bottom->addWidget(createAlarmPanel(), 2);
        root->addLayout(bottom);

        connect(m_model, &TelemetryModel::changed, this, &LiteHmi::refresh);
        connect(&m_clockTimer, &QTimer::timeout, this, &LiteHmi::refreshClock);
        m_clockTimer.start(1000);
        refreshClock();
        refresh();
        qInfo() << "Nexus HMI lite: widgets ready";
    }

private:
    QGroupBox *createStatePanel() {
        auto *box = new QGroupBox("Estado");
        auto *layout = new QVBoxLayout(box);
        layout->setSpacing(8);
        m_connection = makeLabel("--", "statusPill");
        m_motorState = makeLabel("--", "stateValue");
        m_syncState = makeLabel("--", "statusPill");
        layout->addWidget(makeLabel("Control", "metricLabel"));
        layout->addWidget(m_motorState);
        layout->addWidget(makeLabel("Sincronismo", "metricLabel"));
        layout->addWidget(m_syncState);
        layout->addWidget(makeLabel("Comunicacion", "metricLabel"));
        layout->addWidget(m_connection);
        layout->addStretch(1);
        return box;
    }

    QGroupBox *createMeasurementsPanel() {
        auto *box = new QGroupBox("Mediciones");
        auto *grid = new QGridLayout(box);
        grid->setHorizontalSpacing(22);
        grid->setVerticalSpacing(10);
        m_voltage = makeValue(grid, 0, 0, "Voltaje");
        m_current = makeValue(grid, 1, 0, "Corriente");
        m_frequency = makeValue(grid, 2, 0, "Frecuencia");
        m_powerFactor = makeValue(grid, 3, 0, "Factor potencia");
        m_speed = makeValue(grid, 0, 2, "Velocidad");
        m_loadAngle = makeValue(grid, 1, 2, "Angulo carga");
        m_slip = makeValue(grid, 2, 2, "Slip");
        m_discharge = makeValue(grid, 3, 2, "Descarga");
        return box;
    }

    QGroupBox *createSignalsPanel() {
        auto *box = new QGroupBox("Campo y salidas");
        auto *grid = new QGridLayout(box);
        m_fieldVoltage = makeValue(grid, 0, 0, "Field V");
        m_fieldCurrent = makeValue(grid, 1, 0, "Field A");
        m_fs = makeValue(grid, 2, 0, "FS");
        m_fal = makeValue(grid, 3, 0, "FAL");
        m_ok56k = makeValue(grid, 4, 0, "56K");
        m_fwt = makeValue(grid, 5, 0, "FWT");
        m_dst = makeValue(grid, 6, 0, "DST");
        return box;
    }

    QGroupBox *createCommandPanel() {
        auto *box = new QGroupBox("Operacion");
        auto *grid = new QGridLayout(box);
        auto *start = makeCommandButton("START", "startButton");
        auto *stop = makeCommandButton("STOP", "stopButton");
        auto *ack = makeCommandButton("ACK", "neutralButton");
        auto *reset = makeCommandButton("RESET", "resetButton");
        connect(start, &QPushButton::clicked, m_backend, &BackendFacade::startMotor);
        connect(stop, &QPushButton::clicked, m_backend, &BackendFacade::stopMotor);
        connect(ack, &QPushButton::clicked, m_backend, &BackendFacade::ack);
        connect(reset, &QPushButton::clicked, m_backend, &BackendFacade::reset);
        grid->addWidget(start, 0, 0);
        grid->addWidget(stop, 0, 1);
        grid->addWidget(ack, 1, 0);
        grid->addWidget(reset, 1, 1);
        return box;
    }

    QGroupBox *createAlarmPanel() {
        auto *box = new QGroupBox("Alarmas");
        auto *layout = new QVBoxLayout(box);
        m_alarm = makeLabel("--", "alarmText");
        layout->addWidget(m_alarm);
        layout->addWidget(makeLabel("Modo embedded-lite: sin graficas, sin animaciones, sin QML.", "subtitle"));
        return box;
    }

    void refreshClock() {
        m_clock->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd  HH:mm:ss"));
    }

    void refresh() {
        m_motorState->setText(m_model->motorState());
        m_syncState->setText(m_model->syncState());
        m_connection->setText(m_model->connected() ? "CONNECTED" : "NO DATA");
        m_voltage->setText(QString::number(m_model->voltage(), 'f', 1) + " V");
        m_current->setText(QString::number(m_model->current(), 'f', 2) + " A");
        m_frequency->setText(QString::number(m_model->frequency(), 'f', 3) + " Hz");
        m_powerFactor->setText(QString::number(m_model->powerFactor(), 'f', 3));
        m_speed->setText(QString::number(m_model->speedPct(), 'f', 1) + " %");
        m_loadAngle->setText(QString::number(m_model->loadAngle(), 'f', 1) + " deg");
        m_slip->setText(QString::number(m_model->slipHz(), 'f', 2) + " Hz");
        m_discharge->setText(QString::number(m_model->dischargeCurrent(), 'f', 2) + " A");
        m_fieldVoltage->setText(QString::number(m_model->fieldVoltage(), 'f', 1) + " V");
        m_fieldCurrent->setText(QString::number(m_model->fieldCurrent(), 'f', 2) + " A");
        m_fs->setText(yesNo(m_model->fs()));
        m_fal->setText(yesNo(m_model->fal()));
        m_ok56k->setText(yesNo(m_model->ok56k()));
        m_fwt->setText(yesNo(m_model->fwt()));
        m_dst->setText(yesNo(m_model->dst()));
        m_alarm->setText(m_model->alarmText());
    }

    TelemetryModel *m_model = nullptr;
    BackendFacade *m_backend = nullptr;
    QTimer m_clockTimer;
    QLabel *m_clock = nullptr;
    QLabel *m_connection = nullptr;
    QLabel *m_motorState = nullptr;
    QLabel *m_syncState = nullptr;
    QLabel *m_alarm = nullptr;
    QLabel *m_voltage = nullptr;
    QLabel *m_current = nullptr;
    QLabel *m_frequency = nullptr;
    QLabel *m_powerFactor = nullptr;
    QLabel *m_speed = nullptr;
    QLabel *m_loadAngle = nullptr;
    QLabel *m_slip = nullptr;
    QLabel *m_discharge = nullptr;
    QLabel *m_fieldVoltage = nullptr;
    QLabel *m_fieldCurrent = nullptr;
    QLabel *m_fs = nullptr;
    QLabel *m_fal = nullptr;
    QLabel *m_ok56k = nullptr;
    QLabel *m_fwt = nullptr;
    QLabel *m_dst = nullptr;
};

} // namespace

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", qgetenv("QT_QPA_PLATFORM").isEmpty() ? QByteArray("linuxfb:fb=/dev/fb0") : qgetenv("QT_QPA_PLATFORM"));
    qputenv("QT_QPA_FB_DRM", qgetenv("QT_QPA_FB_DRM").isEmpty() ? QByteArray("0") : qgetenv("QT_QPA_FB_DRM"));
    qputenv("QT_QUICK_BACKEND", qgetenv("QT_QUICK_BACKEND").isEmpty() ? QByteArray("software") : qgetenv("QT_QUICK_BACKEND"));

    QApplication app(argc, argv);
    QApplication::setApplicationName("Nexus Sync HMI Lite");
    QApplication::setOrganizationName("Nexus");
    QApplication::setOverrideCursor(Qt::BlankCursor);
    qInfo() << "Nexus HMI lite: application started";
    qInfo() << "Nexus HMI lite: platform" << qgetenv("QT_QPA_PLATFORM") << "fb_drm" << qgetenv("QT_QPA_FB_DRM");

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({ "demo", "Run with local simulated data." });
    parser.addOption({ "axi", "Read telemetry from PL AXI-Lite registers at <base>.", "base" });
    parser.addOption({ "fullscreen", "Start fullscreen." });
    parser.process(app);

    TelemetryModel model;
    std::unique_ptr<DataProvider> provider;
    DemoDataProvider demoProvider;
    if (parser.isSet("axi") && !parser.isSet("demo")) {
        bool ok = false;
        const quintptr base = parser.value("axi").toULongLong(&ok, 0);
        if (ok && base != 0) {
            qInfo() << "Nexus HMI lite: using AXI provider at 0x" + QString::number(base, 16);
            provider.reset(new AxiLiteProvider(base));
        } else {
            qWarning() << "Nexus HMI lite: invalid --axi base, falling back to demo";
        }
    }
    DataProvider *activeProvider = provider ? provider.get() : static_cast<DataProvider *>(&demoProvider);
    BackendFacade backend(&model, activeProvider);

    app.setStyleSheet(R"(
        QWidget#root { background: #08111D; color: #F5F8FC; font-family: DejaVu Sans; }
        QGroupBox {
            background: #0E1726;
            border: 1px solid #203649;
            border-radius: 10px;
            margin-top: 20px;
            padding: 12px;
            color: #DCE7EF;
            font-size: 16px;
            font-weight: 600;
        }
        QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 6px; color: #AFC4D8; }
        QLabel#title { color: #F5F8FC; font-size: 30px; font-weight: 700; }
        QLabel#subtitle { color: #7E92B0; font-size: 14px; }
        QLabel#clock { color: #DCE7EF; font-size: 18px; font-weight: 600; }
        QLabel#metricLabel { color: #7E92B0; font-size: 14px; font-weight: 600; }
        QLabel#metricValue { color: #F5F8FC; font-size: 24px; font-weight: 700; }
        QLabel#stateValue { color: #34D67A; font-size: 36px; font-weight: 800; }
        QLabel#statusPill { color: #38BDF8; font-size: 20px; font-weight: 700; }
        QLabel#alarmText { color: #FFB84D; font-size: 24px; font-weight: 700; }
        QPushButton {
            background: #13202B;
            color: #DCE7EF;
            border: 1px solid #203649;
            border-radius: 8px;
            font-size: 18px;
            font-weight: 800;
        }
        QPushButton#startButton { background: #12351F; color: #34D67A; border-color: #1F7A45; }
        QPushButton#stopButton { background: #3A1418; color: #FF5B5B; border-color: #8A2B32; }
        QPushButton#resetButton { background: #12214A; color: #7FA2FF; border-color: #3158C9; }
        QPushButton#neutralButton { background: #182433; color: #DCE7EF; }
    )");

    LiteHmi window(&model, &backend);
    if (parser.isSet("fullscreen")) {
        qInfo() << "Nexus HMI lite: showing fullscreen";
        window.showFullScreen();
    } else {
        qInfo() << "Nexus HMI lite: showing window";
        window.show();
    }

    activeProvider->start();
    qInfo() << "Nexus HMI lite: provider started";
    return app.exec();
}

#include "embedded_lite_main.moc"
