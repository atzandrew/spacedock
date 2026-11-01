import QtQuick

// Icon reordering: pickup, placement-gap targeting, landing, and the native
// drag handoff when the icon leaves the dock surface.
//
// Coordinates: the pointer is held in dock-window (root) coordinates; artwork
// positions (floatingLeft/Top/Size) are presentation-local, so the reveal
// transform can scale/translate underneath without moving the grabbed pixel.
Item {
    id: drag
    required property Item dock
    required property Item presentation
    required property Item viewport
    required property Item tasks
    // Hand off to a compositor drag image once the artwork reaches the dock
    // surface's edge. Preview windows keep drawing in-window instead.
    property bool nativeHandoff: false
    // True while KWin draws the icon. Positions then arrive as drag events.
    property bool handedOff: false
    onHandedOffChanged: dock.requestInputUpdate()

    readonly property var layout: dock.layout
    readonly property var magnification: dock.magnification

    property string dragAppKey: ""
    property int dragFrom: -1
    property int dragTo: -1
    property int placementCandidate: -1
    property int gapSlot: -1
    readonly property bool dragging: dragFrom >= 0
    readonly property var draggedTask: {
        const revision = dock.delegateRevision
        return dragging ? tasks.itemAt(dragFrom) : null
    }

    readonly property int partingHoverDelayMs: tuning.motion.dragPartingDelayMs
    readonly property int partingDurationMs: tuning.motion.partingDurationMs
    Timer {
        id: partingHover
        interval: drag.partingHoverDelayMs
        onTriggered: {
            if (drag.dragging && !drag.settling && drag.placementCandidate >= 0) {
                drag.gapSlot = drag.placementCandidate
                drag.dragTo = drag.placementCandidate
            }
        }
    }
    property real gapAmount: removing || (dragging && !settling && dragTo < 0) ? 0 : 1
    Behavior on gapAmount {
        NumberAnimation { duration: drag.partingDurationMs }
    }

    property real dragCenter: 0
    property real dragTop: 0
    property real dragSize: 0
    property point dragPointer: Qt.point(0, 0)
    property real dragRevealScale: 1
    readonly property real dragScaleRatio: dragRevealScale / Math.max(0.01, presentation.scale)
    readonly property point dragPosition: {
        const transform = [presentation.x, presentation.y, presentation.scale, presentation.width, presentation.height]
        return presentation.mapFromItem(dock, dragPointer.x, dragPointer.y)
    }
    onDragPositionChanged: {
        if (!dragging) return
        dock.observePointer(dragPointer, true)
        if (!settling) Qt.callLater(updateDragTarget)
    }
    property real grabX: 0
    property real grabY: 0
    property real pickupOffsetX: 0
    property real pickupOffsetY: 0
    property real pickupProgress: 1
    NumberAnimation {
        id: pickup
        target: drag
        property: "pickupProgress"
        from: 0; to: 1
        duration: drag.magnification.enterDurationMs
        easing.type: Easing.OutCubic
    }

    property bool settling: false
    property bool removing: false
    property real removalProgress: 0
    readonly property bool removalReady: dragging && !settling && !handedOff
        && draggedTask && draggedTask.pinned
        && floatingTop + floatingSize <= dock.iconBottom - 4 * layout.iconSize
    property real settleProgress: 0
    property rect releaseBounds: Qt.rect(0, 0, 0, 0)
    readonly property rect releasePlacement: {
        const transform = [presentation.x, presentation.y, presentation.scale, presentation.width, presentation.height]
        return presentation.mapFromItem(dock, releaseBounds)
    }
    property int taskStructureRevision: 0
    property int releaseStructureRevision: 0
    property int landingDurationMs: magnification.enterDurationMs
    property bool returningHome: false
    readonly property var dropPlacement: dock.geometry.items[dragTo] || {x: 0, size: layout.iconSize}
    readonly property real floatingLeft: settling && !removing
        ? releasePlacement.x + (viewport.x - viewport.contentX + dropPlacement.x - releasePlacement.x) * settleProgress
        : dragCenter - (grabX - pickupOffsetX * (1 - pickupProgress)) * dragScaleRatio
    readonly property real floatingTop: settling && !removing
        ? releasePlacement.y + (dock.iconBottom - dropPlacement.size - (draggedTask ? draggedTask.bounceOffset : 0) - releasePlacement.y) * settleProgress
        : dragTop + pickupOffsetY * (1 - pickupProgress) * dragScaleRatio
    readonly property real floatingSize: settling && !removing
        ? releasePlacement.width + (dropPlacement.size - releasePlacement.width) * settleProgress
        : dragSize * dragScaleRatio
    // Artwork in dock-window coordinates.
    readonly property rect floatingBounds: {
        const transform = [presentation.x, presentation.y, presentation.scale, presentation.width, presentation.height]
        return presentation.mapToItem(dock, Qt.rect(floatingLeft, floatingTop, floatingSize, floatingSize))
    }

    function traceDrag(phase, position) {
        if (dockBackend.dragTracing)
            dockBackend.traceDrag(dock.dockWindow, phase, position, floatingBounds,
                Qt.point(grabX * dragRevealScale, grabY * dragRevealScale), pickupProgress)
    }

    // Enabled by --trace-drops. Keep state transitions alongside native event
    // delivery to distinguish a rejected drop from an interrupted landing.
    function traceDrop(phase, position) {
        if (!dockBackend.dropTracing) return
        console.log("drop-trace controller", phase,
            "position", position === undefined ? dragPointer : position,
            "handedOff", handedOff, "settling", settling,
            "from", dragFrom, "to", dragTo, "candidate", placementCandidate)
    }

    // --- Gesture entry points (called by the task delegate) ---

    // Lift `task` once the press has crossed the drag threshold. Transfers
    // the currently drawn artwork bounds so the icon doesn't jump.
    function begin(task, windowPosition, grabFractionX, grabFractionY) {
        const position = presentation.mapFromItem(dock, windowPosition.x, windowPosition.y)
        const artwork = task.mapToItem(presentation, 0, -task.bounceOffset)
        dragPointer = windowPosition
        dragRevealScale = presentation.scale
        dragSize = task.width
        grabX = grabFractionX * task.width
        grabY = grabFractionY * task.height
        pickupOffsetX = artwork.x - (position.x - grabX)
        pickupOffsetY = artwork.y - (position.y - grabY)
        pickupProgress = 0
        dragCenter = Qt.binding(() => drag.dragPosition.x)
        dragTop = Qt.binding(() => drag.dragPosition.y - drag.grabY * drag.dragScaleRatio)
        placementCandidate = -1
        gapSlot = task.index
        dragTo = -1
        dragAppKey = task.appKey
        dragFrom = task.index
        pickup.start()
    }

    function move(windowPosition) {
        if (!dragging || settling || handedOff) return
        dragPointer = windowPosition
        dock.observePointer(dragPointer)
        updateDragTarget()
        traceDrag("move", windowPosition)
        if (nativeHandoff && leavesSurface()) handOff()
    }

    function release(windowPosition) {
        if (!dragging || settling || handedOff) return
        dragPointer = windowPosition
        dock.observePointer(dragPointer, true)
        updateDragTarget()
        traceDrag("release", dragPointer)
        if (removalReady) removePin()
        else landReorder()
    }

    // --- Native handoff ---

    function leavesSurface() {
        const b = floatingBounds
        return b.y < 0 || b.x < 0 || b.x + b.width > dock.width
    }

    function handOff() {
        traceDrop("handoff")
        // Freeze the current grab point and size: the compositor image is a
        // fixed bitmap positioned by the hotspot.
        // If pickup is still easing, finish it at the currently drawn spot.
        const b = floatingBounds
        const local = presentation.mapFromItem(dock, b)
        pickup.stop()
        pickupProgress = 1
        grabX = (dragPosition.x - local.x) / dragScaleRatio
        grabY = (dragPosition.y - local.y) / dragScaleRatio
        handedOff = true
        dockBackend.startTaskDrag(dock.dockWindow, dragFrom, dragAppKey, b.width,
            Qt.point(dragPointer.x - b.x, dragPointer.y - b.y))
    }

    function isOwnDrag(event) {
        return handedOff && event.formats.indexOf(dockBackend.taskDragMimeType) >= 0
    }

    function nativeMove(windowPosition) {
        const previousCandidate = placementCandidate
        dragPointer = windowPosition
        updateDragTarget()
        // Log target changes, not every motion, to limit timing interference.
        if (previousCandidate !== placementCandidate) traceDrop("nativeMove target", windowPosition)
    }

    function nativeLeave() {
        traceDrop("nativeLeave")
        partingHover.stop()
        placementCandidate = -1
        dragTo = -1
    }

    function nativeDrop(windowPosition) {
        traceDrop("nativeDrop", windowPosition)
        dragPointer = windowPosition
        updateDragTarget()
        // The compositor image disappears at this position; the dock's own
        // artwork reappears under the same pixel and glides into the slot.
        handedOff = false
        landReorder()
    }

    Connections {
        target: dockBackend
        // Dropped outside the dock, or cancelled: glide back home from the
        // nearest visible point on the dock surface.
        function onTaskDragFinished(droppedOnDock) {
            drag.traceDrop("taskDragFinished droppedOnDock=" + droppedOnDock)
            // The native drag consumed the press; never leave it looking held.
            drag.dock.pressedTask = -1
            if (!drag.handedOff) return
            drag.handedOff = false
            if (!drag.dragging || drag.settling) return
            const b = drag.floatingBounds
            const left = Math.max(0, Math.min(drag.dock.width - b.width, b.x))
            const top = Math.max(0, Math.min(drag.dock.height - b.height, b.y))
            drag.dragPointer = Qt.point(drag.dragPointer.x + left - b.x,
                                        drag.dragPointer.y + top - b.y)
            drag.nativeLeave()
            drag.landReorder(true)
        }
        // State/icon revisions do not transfer drag ownership. Only topology
        // changes invalidate row indices; identity changes are checked below.
        function onTaskStructureChanged() {
            drag.taskStructureRevision++
            if (drag.dragging && !drag.handedOff) drag.cancelReorder()
        }
    }

    // --- Targeting ---

    function placementBoundary(slot) {
        const items = dock.placementGeometry.items
        if (!items.length) return 0
        if (slot === 0) return items[0].x
        const previous = items[slot - 1]
        if (slot === items.length) return previous.x + previous.size
        return (previous.x + previous.size + items[slot].x) / 2
    }

    function updateDragTarget() {
        if (!dragging || settling) return
        // Compare artwork bounds, not the cursor: grabbing a corner must not
        // open a gap while the icon itself is still far above the row.
        const left = floatingLeft
        const top = floatingTop
        const right = left + floatingSize
        const bottom = top + floatingSize
        const reach = layout.iconSize * 0.2
        const iconBottom = dock.iconBottom
        const geometry = dock.placementGeometry
        let near = false
        // Hit-test the unparted layout. Animated delegates move in response to
        // this decision and must never feed back into their own hover target.
        const origin = viewport.x - viewport.contentX
        for (let i = 0; i < geometry.items.length; ++i) {
            const item = geometry.items[i]
            const x = origin + item.x
            const y = iconBottom - item.size
            near = near || (right >= x - reach && left <= x + item.size + reach
                && bottom >= y - reach && top <= iconBottom + reach)
        }
        // A one-icon dock still has a home to return to.
        if (tasks.count === 1) near = bottom >= iconBottom - layout.iconSize - reach
            && top <= iconBottom + reach && right >= (dock.width - layout.iconSize) / 2 - reach
            && left <= (dock.width + layout.iconSize) / 2 + reach
        let nearest = -1
        let distance = Infinity
        if (near) for (let i = 0; i < tasks.count; ++i) {
            if (!tasks.itemAt(i) || (tasks.itemAt(dragFrom).pinned && !tasks.itemAt(i).pinned)) continue
            const delta = Math.abs(left + floatingSize / 2 - origin - placementBoundary(i))
            if (delta < distance) { distance = delta; nearest = i }
        }
        // Small pointer tremors around a slot boundary retain the current slot.
        if (near && placementCandidate >= 0 && nearest >= 0) {
            const previousDistance = Math.abs(left + floatingSize / 2 - origin - placementBoundary(placementCandidate))
            if (previousDistance <= distance + layout.iconSize * 0.2) nearest = placementCandidate
        }
        if (nearest === placementCandidate) return
        partingHover.stop()
        placementCandidate = nearest
        dragTo = -1
        if (nearest >= 0) partingHover.start()
    }
    Connections {
        target: drag.dock
        function onPlacementGeometryChanged() { if (drag.dragging && !drag.settling) Qt.callLater(drag.updateDragTarget) }
    }
    onPickupProgressChanged: if (dragging && !settling) Qt.callLater(updateDragTarget)

    // Visual row slot for a task while another is lifted.
    function slotFor(index) {
        if (!dragging) return index
        const target = gapSlot < 0 ? dragFrom : gapSlot
        if (index === dragFrom) return target
        if (dragFrom < target && index > dragFrom && index <= target) return index - 1
        if (dragFrom > target && index >= target && index < dragFrom) return index + 1
        return index
    }

    // --- Landing ---

    function removePin() {
        partingHover.stop()
        pickup.stop()
        releaseStructureRevision = taskStructureRevision
        removing = true
        settling = true
        removalProgress = 0
        removal.start()
    }
    NumberAnimation {
        id: removal
        target: drag
        property: "removalProgress"
        from: 0; to: 1; duration: 300
        easing.type: Easing.OutCubic
        onFinished: {
            const row = drag.dragFrom
            const key = drag.dragAppKey
            const valid = drag.taskStructureRevision === drag.releaseStructureRevision
                && drag.draggedTask && drag.draggedTask.appKey === key && drag.draggedTask.pinned
            drag.cancelReorder()
            if (valid) dockBackend.togglePin(row)
        }
    }

    function landReorder(nativeReturn) {
        traceDrop("landReorder")
        if (!dragging || settling) return
        partingHover.stop()
        releaseBounds = floatingBounds
        pickup.stop()
        releaseStructureRevision = taskStructureRevision
        returningHome = nativeReturn === true || (dragTo < 0 && placementCandidate < 0)
        landingDurationMs = Math.max(returningHome ? 450 : magnification.enterDurationMs,
                                     gapAmount < 1 ? partingDurationMs : 0)
        if (dragTo < 0) dragTo = placementCandidate >= 0 ? placementCandidate : dragFrom
        gapSlot = dragTo
        settleProgress = 0
        settling = true
        landing.start()
    }
    NumberAnimation {
        id: landing
        target: drag
        property: "settleProgress"
        from: 0; to: 1
        duration: drag.landingDurationMs
        easing.type: drag.returningHome ? Easing.InOutCubic : Easing.OutCubic
        onFinished: {
            drag.traceDrop("landing finished")
            const from = drag.dragFrom
            const to = drag.dragTo
            const revision = drag.releaseStructureRevision
            const pinning = drag.draggedTask && !drag.draggedTask.pinned
                && tasks.itemAt(to) && tasks.itemAt(to).pinned
            drag.cancelReorder()
            if (drag.taskStructureRevision === revision) {
                if (pinning) dockBackend.pinTaskAt(from, to)
                else dockBackend.reorder(from, to)
            }
            drag.dock.observePointer(drag.dragPointer, true)
        }
    }

    function cancelReorder() {
        traceDrop("cancelReorder")
        // A compositor-owned drag ends through taskDragFinished.
        if (handedOff) return
        partingHover.stop()
        placementCandidate = -1
        landing.stop()
        removal.stop()
        pickup.stop()
        // End visual ownership before resetting coordinates used by the overlay.
        dragFrom = -1
        dragTo = -1
        settling = false
        removing = false
        dock.pressedTask = -1
    }
}
