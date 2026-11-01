import QtQuick
import QtQuick.Controls
import QtQuick.Window

Window {
    id: window
    width: 440
    height: 220
    minimumWidth: 360
    minimumHeight: 200
    title: qsTr("Slow Launch Test — ready")
    color: "#24313b"
    visible: true
    Column {
        anchors.centerIn: parent
        width: parent.width - 48
        spacing: 16
        Text {
            width: parent.width
            text: qsTr("Ready after 3 seconds")
            font.pixelSize: 24
            color: "#f8f8f8"
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            width: parent.width
            text: qsTr("Close this window, then click the test icon in the dock to try again.")
            font.pixelSize: 14
            color: "#d0d9df"
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Close")
            onClicked: window.close()
        }
    }
}
