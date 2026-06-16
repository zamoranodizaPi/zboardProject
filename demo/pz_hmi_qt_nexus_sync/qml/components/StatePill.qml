import QtQuick 2.9

Rectangle {
    property string label: ""
    property bool active: false
    property bool fault: false
    height: 30
    radius: 15
    color: active ? (fault ? "#3a1118" : "#102D1F") : "#111C2E"
    border.color: active ? (fault ? "#FF5B5B" : "#34D67A") : "#203649"

    Row {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 8
        Rectangle {
            width: 9
            height: 9
            radius: 5
            anchors.verticalCenter: parent.verticalCenter
            color: active ? (fault ? "#FF5B5B" : "#34D67A") : "#64748B"
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: label
            color: active ? "#F5F8FC" : "#7E92B0"
            font.pixelSize: 12
            font.bold: true
        }
    }
}
