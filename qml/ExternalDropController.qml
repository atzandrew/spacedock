import QtQuick

// External launcher drops own a separate protocol from internal task reorders.
// Points passed by the view are presentation-local; snapshots use the same
// viewport origin as the delegates that animate from them.
Item {
    id: control
    required property var backend
    required property Item tasks
    required property Item viewport
    required property var placementGeometry
    required property real surfaceWidth
    required property real geometryWidth
    required property int durationMs
    required property int hoverDelayMs
    required property real iconSize
    required property real iconBottom
    required property bool dragging
    signal pointerMoved(point position)
    signal revealRequested()
    signal pointerLeft()
    signal inputInvalidated()
    property bool committing: false
    property var commitPositions: ({})
    property real commitWidth: 0
    property real commitProgress: 0
    function beginCommit() {
        const positions = {}
        for (let i = 0; i < tasks.count; ++i) {
            const task = tasks.itemAt(i)
            if (task) positions[task.appKey] = {x: viewport.x + task.x - viewport.contentX,
                size: task.width, opacity: task.opacity}
        }
        const currentWidth = geometryWidth
        commitAnimation.stop()
        commitPositions = positions
        commitWidth = currentWidth
        commitProgress = 0
        committing = true
        commitQuiet.restart()
    }
    function modelChanged() {
        if (committing && !commitAnimation.running) commitQuiet.restart()
    }
    Timer {
        id: commitQuiet
        interval: 60
        onTriggered: commitAnimation.start()
    }
    NumberAnimation {
        id: commitAnimation
        target: control
        property: "commitProgress"
        from: 0; to: 1
        duration: control.durationMs
        easing.type: Easing.OutCubic
        onFinished: {
            control.slot = -1
            control.committing = false
            control.commitPositions = ({})
        }
    }
    property int slot: -1
    property bool acceptedDrag: false
    property int candidate: -1
    property bool gapOpen: false
    Timer {
        id: hoverTimer
        interval: control.hoverDelayMs
        onTriggered: {
            if (control.acceptedDrag && control.candidate >= 0) {
                control.slot = control.candidate
                control.gapOpen = true
            }
        }
    }
    onAcceptedDragChanged: if (!acceptedDrag && !committing && gapProgress === 0) slot = -1
    property real gapProgress: acceptedDrag && gapOpen ? 1 : 0
    Behavior on gapProgress {
        NumberAnimation { duration: control.durationMs }
    }
    onGapProgressChanged: {
        inputInvalidated()
        if (gapProgress === 0 && !acceptedDrag && !committing) slot = -1
    }
    function insertionSlot(point) {
        // Use the unparted layout so opening the gap cannot change its target.
        const geometry = placementGeometry
        const origin = viewport.x - viewport.contentX
        let lastPinned = -1
        for (let i = 0; i < tasks.count; ++i) {
            const task = tasks.itemAt(i)
            if (!task || !task.pinned) continue
            lastPinned = i
            const item = geometry.items[i]
            if (point.x < origin + item.x + item.size / 2) return i
        }
        return lastPinned + 1
    }
    function pinAction(event) {
        if (event.supportedActions & Qt.CopyAction) return Qt.CopyAction
        if (event.supportedActions & Qt.LinkAction) return Qt.LinkAction
        // Kicker drags favorite references with Move only; accepting that
        // metadata pins a reference. Never accept generic file Move drags.
        if ((event.supportedActions & Qt.MoveAction)
                && (event.formats.indexOf("text/xx-kicker-kastatsfavorite-id") >= 0
                    || event.formats.indexOf("text/xx-kicker-simplefavorite-id") >= 0))
            return Qt.MoveAction
        return Qt.IgnoreAction
    }
    function updatePosition(point) {
        if (control.dragging || !acceptedDrag) return
        pointerMoved(point)
        const next = nearRow(point) ? insertionSlot(point) : -1
        if (next === candidate) return
        hoverTimer.stop()
        candidate = next
        gapOpen = false
        if (next >= 0) hoverTimer.start()
    }
    function nearRow(point) {
        const reach = iconSize * 0.2
        const origin = viewport.x - viewport.contentX
        for (const item of placementGeometry.items) {
            if (point.x >= origin + item.x - reach && point.x <= origin + item.x + item.size + reach
                    && point.y >= iconBottom - item.size - reach && point.y <= iconBottom + reach)
                return true
        }
        return false
    }
    function enter(event, point) {
        event.accepted = event.hasUrls && pinAction(event) !== Qt.IgnoreAction
            && backend.canPinUrls(event.urls)
        if (event.accepted) {
            commitQuiet.stop()
            commitAnimation.stop()
            committing = false
            commitPositions = ({})
            acceptedDrag = true
            revealRequested()
            updatePosition(point)
            event.accept(pinAction(event))
        }
    }
    function move(event, point) {
        if (!acceptedDrag) return
        event.accept(pinAction(event))
        updatePosition(point)
    }
    function leave() { hoverTimer.stop(); candidate = -1; gapOpen = false; acceptedDrag = false }
    function containsDragChanged(containsDrag) {
        if (!containsDrag && !control.dragging) {
            leave()
            pointerLeft()
        }
    }
    function drop(event, point) {
        event.accepted = false
        const action = pinAction(event)
        if (!acceptedDrag || action === Qt.IgnoreAction || !event.hasUrls || !nearRow(point)) {
            leave()
            return
        }
        const target = insertionSlot(point)
        // Replace the placeholder with the real model row in the same turn.
        beginCommit()
        if (backend.pinUrls(event.urls, target < tasks.count ? target : -1)) {
            event.accept(action)
        }
        leave()
    }

    Connections {
        target: control.backend
        function onTaskStructureChanged() { control.modelChanged() }
    }
}
