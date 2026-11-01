import QtQuick

// Owns visibility timing and lifecycle. The view supplies activity and edge
// hover inputs and handles pointer reconciliation and interaction cleanup.
Item {
    id: control
    required property var settings
    required property real surfaceHeight
    required property bool pointerInside
    required property int openMenus
    required property bool taskPressed
    required property bool dragging
    required property bool externalDrag
    required property bool externalCommit
    required property bool setupPending
    required property bool artworkTransitionActive
    required property bool edgeHovered
    property bool autoHideEnabled: settings.enabled
    property bool startingUp: true
    property bool startupRevealing: false
    property bool shuttingDown: false
    property bool hideRequested: true
    property real hideProgress: 1
    readonly property real hideOffset: surfaceHeight * hideProgress
    readonly property real revealScale: 1 - (1 - (settings.hiddenScale ?? 0.8)) * hideOffset / Math.max(1, surfaceHeight)
    readonly property bool holdingOpen: pointerInside || openMenus > 0 || taskPressed || dragging || externalDrag || externalCommit || setupPending || artworkTransitionActive
    signal clearPointer()
    signal startupFinished()
    signal shutdownFinished()

    function beginShutdown() {
        if (shuttingDown) return
        shuttingDown = true
        startingUp = false
        hideDelay.stop()
        revealDelay.stop()
        clearPointer()
        hideRequested = true
        // If already hidden, there is no animation left to wait for.
        if (hideOffset === surfaceHeight) Qt.callLater(control.shutdownFinished)
    }
    function scheduleHide() {
        hideDelay.stop()
        if (startingUp || shuttingDown) return
        if (!autoHideEnabled) { hideRequested = false; return }
        if (holdingOpen) hideRequested = false
        else if (!edgeHovered) hideDelay.start()
    }
    onHoldingOpenChanged: scheduleHide()
    onAutoHideEnabledChanged: { revealDelay.stop(); scheduleHide() }
    onSettingsChanged: scheduleHide()
    onHideRequestedChanged: {
        if (startupRevealing || !startingUp) hideProgress = hideRequested ? 1 : 0
        if (hideRequested) clearPointer()
    }
    onHideOffsetChanged: {
        if (shuttingDown && hideOffset === surfaceHeight) Qt.callLater(control.shutdownFinished)
        else if (startingUp && startupRevealing && hideOffset === 0) {
            startingUp = false
            startupFinished()
            scheduleHide()
        }
    }
    onEdgeHoveredChanged: {
        if (edgeHovered) {
            hideDelay.stop()
            if (hideRequested) revealDelay.start()
        } else {
            revealDelay.stop()
            scheduleHide()
        }
    }
    Behavior on hideProgress {
        NumberAnimation { duration: control.settings.slideDurationMs; easing.type: Easing.InOutCubic }
    }
    Timer {
        id: hideDelay
        interval: control.settings.hideDelayMs
        onTriggered: {
            if (!control.startingUp && !control.shuttingDown && control.autoHideEnabled && !control.holdingOpen && !control.edgeHovered)
                control.hideRequested = true
        }
    }
    Timer {
        id: revealDelay
        interval: control.settings.revealDelayMs
        onTriggered: {
            if (!control.startingUp && !control.shuttingDown && control.edgeHovered && control.autoHideEnabled) {
                control.hideRequested = false
                // Allow time to travel from the edge to the revealed icons.
                control.scheduleHide()
            }
        }
    }
    Component.onCompleted: startupDelay.start()
    Timer {
        id: startupDelay
        interval: 16
        onTriggered: {
            if (!control.shuttingDown) {
                control.startupRevealing = true
                control.hideRequested = false
            }
        }
    }
}
