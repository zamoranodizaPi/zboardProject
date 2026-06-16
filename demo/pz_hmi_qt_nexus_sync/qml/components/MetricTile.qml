import QtQuick 2.9

Rectangle {
    property string label: ""
    property string unit: ""
    property real value: 0
    property color accent: "#34D67A"
    color: "#111C2E"
    radius: 8

    Column {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 4

        Text {
            text: label
            color: "#7E92B0"
            font.pixelSize: 12
            font.bold: true
            elide: Text.ElideRight
            width: parent.width
        }
        Row {
            spacing: 6
            Text {
                text: Number(value).toFixed(Math.abs(value) >= 100 ? 0 : 2)
                color: accent
                font.pixelSize: 30
                font.bold: true
            }
            Text {
                anchors.baseline: parent.children[0].baseline
                text: unit
                color: "#AFC4D8"
                font.pixelSize: 13
            }
        }
    }
}
