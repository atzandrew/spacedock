import QtQuick
import QtQuick.Window

Window {
    id: window
    width: 900
    height: tuning.layout.surfaceHeight
    color: "#24313b"
    title: "spacedock — development preview"
    Main { id: content; anchors.fill: parent; dockWindow: window }
    onClosing: close => { close.accepted = false; content.beginShutdown() }
    visible: true
}
