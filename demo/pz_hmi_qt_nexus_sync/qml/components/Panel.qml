import QtQuick 2.9

Rectangle {
    property alias title: titleText.text
    color: "#0D1823"
    radius: 10
    border.color: "#203649"
    border.width: 1

    Text {
        id: titleText
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 14
        anchors.topMargin: 10
        color: "#7E92B0"
        font.pixelSize: 13
        font.bold: true
    }
}
