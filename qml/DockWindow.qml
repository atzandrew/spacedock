import QtQuick
import QtQuick.Window
import org.kde.layershell 1.0 as LayerShell

Window {
    id: window
    width: screen.width
    // Keep lifted artwork on our surface across the screen so it can return
    // from its release point without a compositor drag-image handoff.
    height: Math.max(screen.height, tuning.layout.surfaceHeight)
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    title: "spacedock"
    LayerShell.Window.anchors: LayerShell.Window.AnchorBottom | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.exclusionZone: content.autoHideEnabled ? 0 : Math.ceil(content.layout.shelfHeight)
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone
    LayerShell.Window.scope: "spacedock"
    Component.onCompleted: {
        // Older LayerShellQt releases do not expose this optional property.
        if ("activateOnShow" in LayerShell.Window)
            LayerShell.Window.activateOnShow = false
        visible = true
    }
    // Beyond the surface edge KWin carries a native drag image.
    Main { id: content; anchors.fill: parent; dockWindow: window; nativeDragHandoff: true }
    onClosing: close => { close.accepted = false; content.beginShutdown() }
    visible: false
}
