import QtQuick 2.9
import QtQuick.Controls 2.2

Rectangle {
    id: root
    property string action: ""
    signal accepted()
    signal cancelled()

    visible: action.length > 0
    anchors.fill: parent
    color: "#AA000000"

    Rectangle {
        width: 360
        height: 210
        radius: 12
        color: "#0D1823"
        border.color: "#203649"
        anchors.centerIn: parent

        Column {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 16
            Text {
                text: "Confirmacion operacional"
                color: "#7E92B0"
                font.pixelSize: 13
                font.bold: true
            }
            Text {
                text: action
                color: action === "STOP" ? "#FF5B5B" : "#34D67A"
                font.pixelSize: 34
                font.bold: true
            }
            Text {
                text: "El comando sera enviado al backend. La logica de seguridad permanece en PL/control."
                color: "#AFC4D8"
                font.pixelSize: 14
                wrapMode: Text.WordWrap
                width: parent.width
            }
            Row {
                spacing: 12
                Button { text: "Cancelar"; width: 150; onClicked: root.cancelled() }
                Button { text: "Confirmar"; width: 150; onClicked: root.accepted() }
            }
        }
    }
}
