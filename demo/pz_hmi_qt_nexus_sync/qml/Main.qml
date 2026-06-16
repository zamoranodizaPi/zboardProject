import QtQuick 2.9
import QtQuick.Window 2.2
import QtQuick.Controls 2.2
import "components"

ApplicationWindow {
    id: root
    width: 1024
    height: 600
    visible: true
    visibility: startFullscreen ? Window.FullScreen : Window.Windowed
    title: "Nexus Sync HMI"
    color: "#08111D"

    property string pendingAction: ""

    Rectangle {
        anchors.fill: parent
        color: "#08111D"

        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 14

            Row {
                width: parent.width
                height: 56
                spacing: 16

                Column {
                    width: 360
                    spacing: 2
                    Text { text: "Nexus Sync"; color: "#F5F8FC"; font.pixelSize: 28; font.bold: true }
                    Text { text: "PZ7020 local HDMI HMI"; color: "#7E92B0"; font.pixelSize: 13 }
                }

                StatePill { width: 165; label: telemetry.motorState; active: true; fault: telemetry.fal }
                StatePill { width: 165; label: telemetry.syncState; active: telemetry.syncState === "SYNCHRONIZED" }
                StatePill { width: 140; label: telemetry.connected ? "COMM OK" : "COMM LOST"; active: telemetry.connected; fault: !telemetry.connected }

                Text {
                    width: 130
                    horizontalAlignment: Text.AlignRight
                    anchors.verticalCenter: parent.verticalCenter
                    text: Qt.formatTime(new Date(), "hh:mm:ss")
                    color: "#AFC4D8"
                    font.pixelSize: 18
                    font.bold: true
                }
            }

            Row {
                width: parent.width
                height: 292
                spacing: 14

                Panel {
                    width: 642
                    height: parent.height
                    title: "Sistema"

                    Rectangle { x: 50; y: 145; width: 130; height: 5; radius: 2; color: "#34D67A" }
                    Rectangle { x: 205; y: 145; width: 90; height: 5; radius: 2; color: "#34D67A" }
                    Rectangle { x: 295; y: 145; width: 38; height: 5; radius: 2; color: "#34D67A" }
                    Rectangle { x: 480; y: 185; width: 96; height: 5; radius: 2; color: telemetry.fs ? "#4D7DFF" : "#203649" }
                    Rectangle { x: 185; y: 122; width: 64; height: 48; radius: 6; color: "#182638"; border.color: "#AFC4D8" }
                    Rectangle { x: 260; y: 128; width: 36; height: 36; radius: 18; color: "#111C2E"; border.color: telemetry.ok56k ? "#34D67A" : "#64748B" }

                    Text { x: 44; y: 80; text: "RED"; color: "#AFC4D8"; font.bold: true; font.pixelSize: 16 }
                    Text { x: 188; y: 80; text: "BREAKER"; color: "#AFC4D8"; font.bold: true; font.pixelSize: 16 }
                    Text { x: 322; y: 80; text: "MOTOR"; color: "#F5F8FC"; font.bold: true; font.pixelSize: 18 }
                    Text { x: 486; y: 142; text: "FIELD"; color: "#AFC4D8"; font.bold: true; font.pixelSize: 16 }

                    Rectangle {
                        x: 330; y: 118; width: 150; height: 84; radius: 42
                        color: telemetry.motorState === "RUNNING" ? "#123722" : "#13202B"
                        border.color: telemetry.motorState === "RUNNING" ? "#34D67A" : "#203649"
                        Text {
                            anchors.centerIn: parent
                            text: Number(telemetry.speedPct).toFixed(1) + "%\n" + telemetry.motorState
                            horizontalAlignment: Text.AlignHCenter
                            color: "#F5F8FC"
                            font.pixelSize: 16
                            font.bold: true
                        }
                    }
                }

                Panel {
                    width: 320
                    height: parent.height
                    title: "Operacion"

                    Column {
                        anchors.fill: parent
                        anchors.margins: 18
                        anchors.topMargin: 48
                        spacing: 12
                        Row {
                            spacing: 10
                            Button { text: "START"; width: 132; height: 54; onClicked: pendingAction = "START" }
                            Button { text: "STOP"; width: 132; height: 54; onClicked: pendingAction = "STOP" }
                        }
                        Row {
                            spacing: 10
                            Button { text: "ACK"; width: 132; height: 46; onClicked: backend.ack() }
                            Button { text: "RESET"; width: 132; height: 46; onClicked: pendingAction = "RESET" }
                        }
                        StatePill { width: 276; label: "56K OK"; active: telemetry.ok56k }
                        StatePill { width: 276; label: "FS FIELD"; active: telemetry.fs }
                        StatePill { width: 276; label: "FWT / DST"; active: telemetry.fwt || telemetry.dst }
                        StatePill { width: 276; label: telemetry.alarmText; active: telemetry.fal; fault: telemetry.fal }
                    }
                }
            }

            Grid {
                width: parent.width
                height: 130
                columns: 4
                rows: 2
                spacing: 10
                MetricTile { width: 238; height: 60; label: "Voltage"; unit: "V"; value: telemetry.voltage }
                MetricTile { width: 238; height: 60; label: "Current"; unit: "A"; value: telemetry.current }
                MetricTile { width: 238; height: 60; label: "Frequency"; unit: "Hz"; value: telemetry.frequency; accent: "#38BDF8" }
                MetricTile { width: 238; height: 60; label: "Power Factor"; unit: "pu"; value: telemetry.powerFactor; accent: "#FFB84D" }
                MetricTile { width: 238; height: 60; label: "Load Angle"; unit: "deg"; value: telemetry.loadAngle; accent: "#FFB84D" }
                MetricTile { width: 238; height: 60; label: "Field Voltage"; unit: "V"; value: telemetry.fieldVoltage; accent: "#4D7DFF" }
                MetricTile { width: 238; height: 60; label: "Field Current"; unit: "A"; value: telemetry.fieldCurrent; accent: "#4D7DFF" }
                MetricTile { width: 238; height: 60; label: "Discharge"; unit: "A"; value: telemetry.dischargeCurrent; accent: "#FFB84D" }
            }

            Panel {
                width: parent.width
                height: 72
                title: "Tendencia compacta"
                Row {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    anchors.bottomMargin: 14
                    height: 26
                    spacing: 3
                    Repeater {
                        model: Math.min(80, telemetry.trend.length)
                        Rectangle {
                            width: 8
                            height: 6 + Math.min(20, telemetry.current * 2)
                            anchors.bottom: parent.bottom
                            radius: 1
                            color: "#34D67A"
                            opacity: 0.35 + index / 130
                        }
                    }
                }
            }
        }
    }

    ConfirmDialog {
        action: pendingAction
        onCancelled: pendingAction = ""
        onAccepted: {
            if (pendingAction === "START") backend.startMotor()
            else if (pendingAction === "STOP") backend.stopMotor()
            else if (pendingAction === "RESET") backend.reset()
            pendingAction = ""
        }
    }
}
