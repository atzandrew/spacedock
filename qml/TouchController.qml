import QtQuick

// Owns touch grabs above the mouse-only interaction areas. Accepted touches
// never become synthetic mouse clicks or enter the mouse reorder path.
MultiPointTouchArea {
    id: control
    required property var dock
    anchors.fill: parent
    z: 900
    mouseEnabled: false
    enabled: !dock.trashConfirmationPending && !dock.shuttingDown
    objectName: "dockTouchController"
    property bool active: false
    property bool usingTouch: false
    property bool labelsReady: false
    property bool magnificationReady: false
    property bool consumed: false
    property int contactCount: 0
    property int primaryId: -1
    property point holdPosition
    property point position
    property int targetIndex: -1

    function stopTimers() {
        labelDelay.stop()
        magnificationDelay.stop()
        menuDelay.stop()
    }
    function consume() {
        consumed = true
        stopTimers()
        labelsReady = false
        targetIndex = -1
    }
    function restartMenuHold() {
        menuDelay.stop()
        holdPosition = position
        if (active && !consumed && targetIndex >= 0) menuDelay.start()
    }
    function refreshPosition() {
        if (!active || consumed) return
        dock.trackTouch(position)
        const previousTarget = targetIndex
        targetIndex = dock.touchIndexAt(position)
        // A hold belongs to the current icon, including when animated layout
        // or reveal moves a different icon under a stationary finger.
        if (targetIndex !== previousTarget) restartMenuHold()
    }
    function move(point) {
        position = Qt.point(point.x, point.y)
        refreshPosition()
        // Measure against the most recent hold anchor. Small jitter is idle;
        // meaningful movement starts a fresh 750 ms dwell, even after browsing.
        if (active && !consumed && Math.hypot(position.x - holdPosition.x,
                position.y - holdPosition.y) > Qt.styleHints.startDragDistance)
            restartMenuHold()
    }
    function finish() {
        stopTimers()
        active = false
        targetIndex = -1
        labelsReady = false
        magnificationReady = false
        dock.pointerInside = false
        dock.tooltipReady = false
    }
    onPressed: points => {
        contactCount += points.length
        if (contactCount !== 1 || active) { consume(); return }
        usingTouch = true
        active = true
        consumed = dock.openMenus > 0 || dock.dragging || dock.startingUp
        primaryId = points[0].pointId
        position = Qt.point(points[0].x, points[0].y)
        holdPosition = position
        dock.beginTouch()
        if (consumed) return
        refreshPosition()
        labelDelay.start()
        magnificationDelay.start()
    }
    onUpdated: points => {
        for (const point of points)
            if (point.pointId === primaryId) move(point)
    }
    onReleased: points => {
        contactCount = Math.max(0, contactCount - points.length)
        for (const point of points) {
            if (point.pointId !== primaryId) continue
            move(point)
            const target = targetIndex
            const launch = active && !consumed && dock.openMenus === 0
            finish()
            if (launch && target >= 0) dock.activateTouchTarget(target)
        }
        if (contactCount === 0) finish()
    }
    onCanceled: { contactCount = 0; consume(); finish() }
    onEnabledChanged: if (!enabled && active) { contactCount = 0; consume(); finish() }
    onGestureStarted: gesture => gesture.grab()
    // mapFromItem does not observe transforms itself. Re-sample after reveal,
    // resize and magnification bindings settle, even with a stationary finger.
    Connections {
        target: control.dock
        function onHideOffsetChanged() { if (control.active && !control.consumed) Qt.callLater(control.refreshPosition) }
        function onWidthChanged() { if (control.active && !control.consumed) Qt.callLater(control.refreshPosition) }
        function onHeightChanged() { if (control.active && !control.consumed) Qt.callLater(control.refreshPosition) }
        function onGeometryChanged() { if (control.active && !control.consumed) Qt.callLater(control.refreshPosition) }
    }
    Connections {
        target: dockBackend
        function onTaskStructureChanged() { if (control.active) control.consume() }
    }
    Timer { id: labelDelay; interval: 150; onTriggered: control.labelsReady = true }
    Timer { id: magnificationDelay; interval: 300; onTriggered: control.magnificationReady = true }
    Timer {
        id: menuDelay
        interval: 750
        onTriggered: {
            const target = control.targetIndex
            if (target < 0) return
            // Consume before opening a native popup: its grab can cancel ours.
            control.consume()
            control.dock.openTouchMenu(target)
        }
    }
}
