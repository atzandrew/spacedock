import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Dialogs
import "Magnification.js" as Magnification

Item {
    id: root
    objectName: "dockContent"
    required property var dockWindow
    function openSettings() { settingsWindow.show(); settingsWindow.raise(); settingsWindow.requestActivate() }
    function editApplication(row) { applicationSettings.edit(row) }
    DockSettings { id: settingsWindow; dock: root }
    ApplicationSettings { id: applicationSettings }
    readonly property real maximumIconSize: {
        const original = tuning.layout
        const ratio = 128 / original.iconSize
        return Magnification.fitIconSize(tasks.count + 1, 128, original.spacing * ratio,
            (original.padding + (groupBoundary ? original.iconSize * 0.15 : 0)) * ratio, width, 18)
    }
    readonly property var layout: {
        const original = tuning.layout
        const effective = Magnification.fitIconSize(tasks.count + 1, original.iconSize, original.spacing,
            original.padding + (groupBoundary ? original.iconSize * 0.15 : 0), width, 18)
        if (effective >= original.iconSize) return original
        const adjusted = Object.assign({}, original)
        const ratio = effective / original.iconSize
        for (const key of ["iconSize", "spacing", "padding", "shelfHeight", "shelfDepth", "perspectiveInset", "shelfLip", "iconBaselineInset"])
            adjusted[key] = original[key] * ratio
        return adjusted
    }
    readonly property var autoHideSettings: tuning.autoHide
    property alias autoHideEnabled: autoHideControl.autoHideEnabled
    readonly property alias startingUp: autoHideControl.startingUp
    readonly property alias startupRevealing: autoHideControl.startupRevealing
    readonly property alias shuttingDown: autoHideControl.shuttingDown
    property alias hideRequested: autoHideControl.hideRequested
    property alias hideProgress: autoHideControl.hideProgress
    readonly property alias hideOffset: autoHideControl.hideOffset
    readonly property alias revealScale: autoHideControl.revealScale
    readonly property alias holdingOpen: autoHideControl.holdingOpen
    signal shutdownFinished()
    function beginShutdown() {
        if (shuttingDown) return
        cancelReorder()
        dockBackend.cancelAppImageSetup()
        appImageTransition.cancel()
        autoHideControl.beginShutdown()
    }
    readonly property real edgeWidth: Math.min(width, (tasks.count + 1) * (layout.iconSize + layout.spacing)
        - layout.spacing + 2 * layout.padding + groupGap)
    onAutoHideEnabledChanged: requestInputUpdate()
    onAutoHideSettingsChanged: requestInputUpdate()
    onHideOffsetChanged: requestInputUpdate()
    AutoHideController {
        id: autoHideControl
        settings: root.autoHideSettings
        surfaceHeight: root.height
        pointerInside: root.pointerInside
        openMenus: root.openMenus
        taskPressed: root.pressedTask >= 0 || touchControl.active
        dragging: root.dragging
        externalDrag: appDrop.containsDrag
        externalCommit: externalDropControl.committing || root.trashConfirmationPending || applicationSettings.iconPickerVisible
        setupPending: dockBackend.appImageSetupPending
        artworkTransitionActive: appImageTransition.active
        edgeHovered: edgePointer.hovered
        onClearPointer: { root.pointerInside = false; root.tooltipReady = false }
        onStartupFinished: root.observePointer(pointer.point.position)
        onShutdownFinished: root.shutdownFinished()
    }
    Item {
        id: edge
        x: (root.width - width) / 2
        y: root.height - height
        width: root.edgeWidth
        height: root.autoHideSettings.edgeSize
        z: 20
        visible: root.autoHideEnabled
        HoverHandler {
            id: edgePointer
        }
    }
    readonly property real iconBottom: height - layout.iconBaselineInset
    readonly property var magnification: tuning.magnification
    readonly property var tooltipSettings: tuning.tooltip
    readonly property var feedbackSettings: tuning.feedback
    property int pressedTask: -1
    // Drag-and-drop reordering lives in DragController. These aliases keep
    // the dock's public surface (used by layout bindings and tests) stable.
    property alias nativeDragHandoff: dragControl.nativeHandoff
    readonly property alias dragging: dragControl.dragging
    readonly property alias draggedTask: dragControl.draggedTask
    property alias dragFrom: dragControl.dragFrom
    property alias dragTo: dragControl.dragTo
    property alias gapSlot: dragControl.gapSlot
    property alias placementCandidate: dragControl.placementCandidate
    readonly property alias gapAmount: dragControl.gapAmount
    readonly property alias partingDurationMs: dragControl.partingDurationMs
    property alias dragCenter: dragControl.dragCenter
    property alias dragTop: dragControl.dragTop
    property alias dragSize: dragControl.dragSize
    readonly property alias dragPointer: dragControl.dragPointer
    property alias grabX: dragControl.grabX
    readonly property alias grabY: dragControl.grabY
    property alias pickupProgress: dragControl.pickupProgress
    readonly property alias settling: dragControl.settling
    readonly property alias releaseBounds: dragControl.releaseBounds
    readonly property alias floatingLeft: dragControl.floatingLeft
    readonly property alias floatingTop: dragControl.floatingTop
    readonly property alias floatingSize: dragControl.floatingSize
    readonly property alias floatingBounds: dragControl.floatingBounds
    function updateDragTarget() { dragControl.updateDragTarget() }
    function landReorder() { dragControl.landReorder() }
    function cancelReorder() { dragControl.cancelReorder() }
    DragController {
        id: dragControl
        dock: root
        presentation: presentation
        viewport: viewport
        tasks: tasks
    }
    Keys.onEscapePressed: event => {
        if (activeFolderView) { activeFolderView.close(); event.accepted = true }
        else if (dragging && !dragControl.handedOff) { cancelReorder(); event.accepted = true }
    }
    property int delegateRevision: 0
    function refreshDelegateRevision() { delegateRevision++ }
    property bool tooltipReady: false
    // Parent animation players to the dock, not replaceable model delegates.
    property var bouncePlayers: Object.create(null)
    function bounceFor(key) {
        if (!bouncePlayers[key]) bouncePlayers[key] = bounceFactory.createObject(root)
        return bouncePlayers[key]
    }
    function synchronizeFeedback() {
        const states = Object.create(null)
        for (let i = 0; i < tasks.count; ++i) {
            const task = tasks.itemAt(i)
            if (!task) continue
            if (!states[task.appKey]) states[task.appKey] = {loading: false, attention: false, ready: false}
            const state = states[task.appKey]
            state.loading = state.loading || task.loading
            state.ready = state.ready || task.running || task.active
            state.attention = state.attention || (task.attention && !task.active)
        }
        for (const key of Object.keys(bouncePlayers)) {
            const player = bouncePlayers[key]
            const state = states[key]
            player.launchActive = state ? state.loading && !state.ready : false
            player.attentionActive = state ? state.attention : false
            if (!state && !player.playing) {
                delete bouncePlayers[key]
                player.destroy()
            }
        }
    }
    Component {
        id: bounceFactory
        Bounce {
            objectName: "taskBounce"
            settings: root.feedbackSettings
            onPlayingChanged: Qt.callLater(root.synchronizeFeedback)
        }
    }
    readonly property int hoveredIndex: {
        if (touchControl.usingTouch) return openMenus === 0 ? touchControl.targetIndex : -1
        if (!pointerInside || openMenus > 0 || menuPointerReconcilePending || pressedTask >= 0 || dragging || appDrop.containsDrag || externalDropControl.committing || layoutInput.pointerX < viewport.x || layoutInput.pointerX > viewport.x + viewport.width)
            return -1
        let best = -1
        let distance = Infinity
        for (let i = 0; i < geometry.items.length; ++i) {
            const item = geometry.items[i]
            const left = viewport.x + item.x - viewport.contentX
            if (left + item.size <= viewport.x || left >= viewport.x + viewport.width) continue
            const next = Math.abs(layoutInput.pointerX - left - item.size / 2)
            if (next < distance) { best = i; distance = next }
        }
        return best
    }
    readonly property var hoveredTask: {
        const revision = delegateRevision
        return hoveredIndex >= 0 ? tasks.itemAt(hoveredIndex) : null
    }
    function scheduleTooltip() {
        tooltipDelay.stop()
        tooltipReady = false
        if (touchControl.usingTouch || !tooltipSettings.enabled || hoveredIndex < 0) return
        if (tooltipSettings.delayMs === 0) tooltipReady = true
        else tooltipDelay.start()
    }
    onHoveredIndexChanged: scheduleTooltip()
    onTooltipSettingsChanged: scheduleTooltip()
    Timer {
        id: tooltipDelay
        interval: root.tooltipSettings.delayMs
        onTriggered: root.tooltipReady = root.hoveredIndex >= 0
    }
    property real pointerX: width / 2
    property bool pointerInside: false
    // Opt-in profiling workload; real pointer observations must not interrupt it.
    property bool profileHoverRunning: false
    property real profileHoverPeriodMs: 4000
    property int profileHoverObservations: 0
    FrameAnimation {
        running: root.profileHoverRunning
        onTriggered: {
            const next = root.width / 2 + root.edgeWidth * 0.4
                * Math.sin(elapsedTime * 2000 * Math.PI / root.profileHoverPeriodMs)
            if (root.profileHoverObservations > 0) {
                const previous = root.pointerX
                for (let i = 1; i <= root.profileHoverObservations; ++i) {
                    const x = previous + (next - previous) * i / root.profileHoverObservations
                    root.observePointer(root.dockPresentationPoint(x, root.iconBottom - 1), true, true)
                }
            } else {
                root.pointerInside = true
                root.pointerX = next
            }
        }
    }
    function dockPresentationPoint(x, y) { return presentation.mapToItem(root, x, y) }
    property bool trashConfirmationPending: false
    property int openMenus: 0
    property var activeFolderView: null
    onActiveFolderViewChanged: requestInputUpdate()
    property bool menuPointerReconcilePending: false
    MouseArea {
        objectName: "trashConfirmationShield"
        anchors.fill: parent
        z: 1000
        visible: dockBackend.trash.confirmationVisible
        acceptedButtons: Qt.AllButtons
        onPressed: dockBackend.trash.focusConfirmation()
    }
    Connections {
        target: dockBackend.trash
        function onConfirmationVisibleChanged() {
            if (!dockBackend.trash.confirmationVisible) {
                root.trashConfirmationPending = false
                Qt.callLater(root.refreshPointer)
            }
        }
    }
    MouseArea {
        objectName: "iconPickerDockShield"
        anchors.fill: parent
        z: 1001
        visible: applicationSettings.iconPickerVisible
        acceptedButtons: Qt.AllButtons
        onPressed: applicationSettings.focusIconPicker()
    }
    // Root menus own idempotent leases; the dock owns their shared effects.
    // Submenus share the root lease and never change the count.
    // Wayland places a menu once, when it opens, so the icon layout must not
    // move while one is open: freeze magnification exactly where it is.
    function acquireMenuHold() {
        if (openMenus++ === 0) {
            motion.stop(); menuPointerMotion.stop(); pointerCoalescer.stop(); menuPointerReconcilePending = false
            applyLayoutInput()
            // Task menu anchors remap lazily; settle them where icons froze.
            for (let i = 0; i < tasks.count; ++i) {
                const task = tasks.itemAt(i)
                if (task) task.syncMenuAnchor()
            }
        }
    }
    function releaseMenuHold() {
        if (openMenus === 1) menuPointerReconcilePending = true
        openMenus = Math.max(0, openMenus - 1)
        // Destruction may precede closed. Reconcile after delegates/input settle,
        // querying the system cursor because native grabs retain stale hover.
        if (openMenus === 0) {
            if (!trashConfirmationPending) Qt.callLater(root.refreshPointer)
        }
    }
    function refreshPointer() {
        observePointer(dockBackend.cursorPosition(dockWindow), true)
        menuPointerReconcilePending = false
        if (openMenus === 0) resumeMagnification()
    }
    property real amount: 0
    // The layout's inputs. Entering and leaving change both pointerX and
    // amount each frame; solving the layout once for both halves that work.
    property var layoutInput: ({pointerX: 0, amount: 0})
    property bool layoutInputPending: false
    onPointerXChanged: scheduleLayoutInput()
    onAmountChanged: scheduleLayoutInput()
    function scheduleLayoutInput() {
        if (layoutInputPending) return
        layoutInputPending = true
        Qt.callLater(applyLayoutInput)
    }
    function applyLayoutInput() {
        layoutInputPending = false
        if (layoutInput.pointerX !== pointerX || layoutInput.amount !== amount)
            layoutInput = {pointerX: pointerX, amount: amount}
    }
    property real transitionFrom: 0
    property real transitionTo: 0
    readonly property bool magnificationFits: Magnification.canMagnify(tasks.count + 1, layout.iconSize,
        layout.spacing, layout.padding + groupGap / 2, width, magnification.scaleSamples)
        && Math.ceil(layout.iconSize * magnification.scaleSamples[0] + layout.iconBaselineInset
            + (feedbackSettings.bounceEnabled ? feedbackSettings.bounceHeight : 0)
            + (tooltipSettings.enabled ? tooltipSettings.height + tooltipSettings.gap + tooltipSettings.margin : 0)) <= height
    readonly property bool magnifying: magnification.enabled && magnificationFits && pointerInside
        && (!touchControl.usingTouch || (touchControl.active && touchControl.magnificationReady))
    // The resting row (every task plus the trash, no insertion gap). Hovering
    // needs only this layout, so share it rather than solving it three times.
    readonly property var restingLayout: Magnification.layout(tasks.count + 1, layout.iconSize, layout.spacing,
        width - groupGap, layout.padding, layoutInput.pointerX - groupGap / 2, layoutInput.amount, magnification.scaleSamples)
    readonly property var placementGeometry: dragging
        ? addGroupGap(Magnification.layout(tasks.count, layout.iconSize, layout.spacing,
            width - groupGap, layout.padding, layoutInput.pointerX - groupGap / 2, layoutInput.amount, magnification.scaleSamples),
            groupBoundary ? groupBoundary.right.index - (dragFrom < groupBoundary.right.index ? 1 : 0) : 0)
        : committedGeometry
    readonly property var insertionGeometry: externalDropControl.slot < 0 && (!dragging || gapSlot < 0)
        ? restingLayout
        : Magnification.insertionLayout(tasks.count + 1 + (externalDropControl.slot >= 0 ? 1 : 0), layout.iconSize, layout.spacing,
            width - groupGap, layout.padding, layoutInput.pointerX - groupGap / 2, layoutInput.amount, magnification.scaleSamples,
            dragging ? gapSlot : externalDropControl.slot, dragging ? gapAmount : externalDropControl.gapProgress)
    readonly property var committedGeometry: addGroupGap(restingLayout, groupBoundary ? groupBoundary.right.index : 0)
    readonly property var geometry: externalDropControl.committing
        ? {items: committedGeometry.items,
            width: externalDropControl.commitWidth + (committedGeometry.width - externalDropControl.commitWidth) * externalDropControl.commitProgress}
        : addGroupGap(insertionGeometry, groupBoundarySlot)
    readonly property var hoverAreas: Magnification.hoverRects(geometry.items,
        viewport.x - viewport.contentX, iconBottom, viewport.x, viewport.x + viewport.width)
    readonly property var groupBoundary: {
        const revision = delegateRevision
        for (let i = 1; i < tasks.count; ++i) {
            const previous = tasks.itemAt(i - 1)
            const next = tasks.itemAt(i)
            if (previous && next && previous.pinned && !next.pinned)
                return {left: previous, right: next}
        }
        return null
    }
    readonly property real groupGap: groupBoundary ? layout.iconSize * 0.3 : 0
    // An unpinned task dragged into the pinned group joins its preview gap.
    readonly property int groupBoundarySlot: !groupBoundary ? 0 : dragging
        ? groupBoundary.right.index + (dragFrom >= groupBoundary.right.index && gapSlot < groupBoundary.right.index ? 1 : 0)
        : groupBoundary.right.visualSlot
    readonly property real groupFenceX: {
        if (!groupBoundary) return 0
        if (!dragging) return (groupBoundary.left.x + groupBoundary.left.width + groupBoundary.right.x) / 2
        const left = geometry.items[groupBoundarySlot - 1]
        const right = geometry.items[groupBoundarySlot]
        if (!left || !right) return 0
        return (left.x + left.size * (left.weight === undefined ? 1 : left.weight) + right.x) / 2
    }
    function addGroupGap(source, boundary) {
        if (!groupBoundary) return source
        return {items: source.items.map((item, i) => Object.assign({}, item,
                    {x: item.x + (i >= boundary ? groupGap : 0)})),
                width: source.width + groupGap}
    }
    readonly property real shelfWidth: Math.min(width, (closeAnimation.active
        ? closeAnimation.startWidth + (geometry.width - closeAnimation.startWidth) * closeAnimation.progress
        : geometry.width) + 2 * layout.padding)

    QtObject {
        id: closeAnimation
        objectName: "closeAnimation"
        property bool active: false
        property real progress: 0
        property real startWidth: 0
        property real trashX: 0
        property real trashSize: 0
        property var positions: ({})
        property var ghosts: []
        function prepare(first, last) {
            if (root.dragging || externalDropControl.committing) return
            const positions = {}, ghosts = []
            for (let i = 0; i < tasks.count; ++i) {
                const task = tasks.itemAt(i)
                if (!task) continue
                const bounds = task.mapToItem(presentation, 0, 0)
                positions[task.appKey] = {x: bounds.x, size: task.width}
                if (i >= first && i <= last && !task.pinned)
                    ghosts.push({x: bounds.x, y: bounds.y - task.bounceOffset, size: task.width,
                        source: task.iconUrl, placeholder: task.needsAppImageSetup,
                        openProgress: task.appImageOpenProgress})
            }
            if (!ghosts.length) return
            closeMotion.stop()
            startWidth = root.geometry.width
            trashX = trashIcon.mapToItem(presentation, 0, 0).x
            trashSize = trashIcon.width
            closeAnimation.positions = positions
            closeAnimation.ghosts = ghosts
            progress = 0
            active = true
            Qt.callLater(() => closeMotion.restart())
        }
    }
    NumberAnimation {
        id: closeMotion
        target: closeAnimation; property: "progress"
        from: 0; to: 1; duration: root.partingDurationMs
        easing.type: Easing.OutCubic
        onFinished: { closeAnimation.active = false; closeAnimation.ghosts = []; closeAnimation.positions = ({}) }
    }
    Connections {
        target: dockBackend
        function onRowsAboutToBeRemoved(parent, first, last) {
            if (!parent.valid) closeAnimation.prepare(first, last)
        }
    }

    function observePointer(position, refresh = false, simulated = false) {
        if (touchControl.usingTouch || (profileHoverRunning && !simulated)) return
        position = presentation.mapFromItem(root, position.x, position.y)
        if (startingUp || shuttingDown || hideRequested || trashConfirmationPending) { pointerInside = false; return }
        if (openMenus > 0) return
        if (menuPointerReconcilePending && !refresh) return
        if (!pointer.hovered && !dragging && !refresh) {
            if (!menuPointerMotion.running) pointerInside = false
            return
        }
        let inside = position.y >= shelf.y && position.y <= height && position.x >= shelf.x && position.x <= shelf.x + shelf.width
        for (const area of (dragging ? hoverAreas : currentHoverAreas())) {
            inside = inside || (position.x >= area.x && position.x <= area.x + area.width
                && position.y >= area.y && position.y <= area.y + area.height)
        }
        // Preserve the last focus while leaving, rather than sweeping the peak
        // to an unrelated position as the exit animation begins.
        if (inside) {
            if (((refresh && menuPointerReconcilePending && amount > 0) || menuPointerMotion.running)
                    && position.x !== pointerX) {
                if (!menuPointerMotion.running || position.x !== menuPointerMotion.to) {
                    menuPointerMotion.stop()
                    menuPointerMotion.to = position.x
                    menuPointerMotion.restart()
                }
            } else {
                menuPointerMotion.stop()
                pointerX = position.x
            }
        } else {
            menuPointerMotion.stop()
        }
        pointerInside = inside
    }
    TouchController { id: touchControl; dock: root }
    function beginTouch() {
        pointerCoalescer.stop()
        menuPointerMotion.stop()
        tooltipDelay.stop()
        tooltipReady = false
        menuPointerReconcilePending = false
        if (settling) cancelReorder()
        // A finger at the reveal strip should reveal and hold the dock open.
        hideRequested = false
    }
    function touchIndexAt(windowPosition) {
        const position = presentation.mapFromItem(root, windowPosition.x, windowPosition.y)
        let inside = position.y >= shelf.y && position.y <= height
            && position.x >= shelf.x && position.x <= shelf.x + shelf.width
        for (const area of currentHoverAreas())
            inside = inside || (position.x >= area.x && position.x <= area.x + area.width
                && position.y >= area.y && position.y <= area.y + area.height)
        if (!inside || position.x < viewport.x || position.x > viewport.x + viewport.width) return -1
        let best = -1
        let distance = Infinity
        for (let i = 0; i < geometry.items.length; ++i) {
            const item = geometry.items[i]
            const left = viewport.x + item.x - viewport.contentX
            if (left + item.size <= viewport.x || left >= viewport.x + viewport.width) continue
            const next = Math.abs(position.x - left - item.size / 2)
            if (next < distance) { best = i; distance = next }
        }
        return best
    }
    function trackTouch(position) {
        pointerInside = touchIndexAt(position) >= 0
        if (pointerInside) pointerX = presentation.mapFromItem(root, position.x, position.y).x
    }
    function activateTouchTarget(index) {
        if (index === tasks.count) dockBackend.openTrash()
        else {
            const task = tasks.itemAt(index)
            if (task) task.activateItem()
        }
    }
    function openTouchMenu(index) {
        if (index === tasks.count) trashMenu.openForTouch()
        else {
            const task = tasks.itemAt(index)
            if (task) task.openTouchContextMenu()
        }
    }
    NumberAnimation {
        id: menuPointerMotion
        target: root; property: "pointerX"
        duration: root.magnification.enterDurationMs
        easing.type: Easing.OutCubic
        onFinished: Qt.callLater(root.refreshPointer)
    }
    onMagnifyingChanged: if (openMenus === 0) resumeMagnification()
    function resumeMagnification() {
        const target = magnifying ? 1 : 0
        if (amount === target && !motion.running) return
        transitionFrom = amount
        transitionTo = target
        motion.restart()
    }
    onHoverAreasChanged: requestInputUpdate()

    // Mice can report several motions per frame, and each one re-evaluates the
    // magnified layout. Apply only the latest hover motion, once per frame.
    // Enter, leave, drags, presses, and menus stay immediate.
    property point pendingPointer
    FrameAnimation {
        id: pointerCoalescer
        onTriggered: root.flushPointer()
    }
    function flushPointer() {
        if (!pointerCoalescer.running) return
        pointerCoalescer.stop()
        observePointer(pendingPointer)
    }
    onPressedTaskChanged: flushPointer()
    HoverHandler {
        id: pointer
        onPointChanged: {
            if (touchControl.active) return
            touchControl.usingTouch = false
            if (root.dragging || !hovered) {
                pointerCoalescer.stop()
                root.observePointer(point.position)
                return
            }
            root.pendingPointer = point.position
            if (!pointerCoalescer.running) pointerCoalescer.start()
        }
        onHoveredChanged: {
            if (touchControl.usingTouch) return
            pointerCoalescer.stop()
            root.observePointer(point.position)
        }
    }
    FrameAnimation {
        id: motion
        onTriggered: {
            const duration = root.transitionTo > root.transitionFrom
                ? root.magnification.enterDurationMs : root.magnification.exitDurationMs
            const samples = root.transitionTo > root.transitionFrom
                ? root.magnification.enterProgressSamples : root.magnification.exitProgressSamples
            const progress = Magnification.transition(elapsedTime * 1000, duration, samples)
            root.amount = root.transitionFrom + (root.transitionTo - root.transitionFrom) * progress
            if (elapsedTime * 1000 >= duration) stop()
        }
    }

    function currentHoverAreas() {
        const areas = hoverAreas.slice()
        for (let i = 0; i < tasks.count; ++i) {
            const task = tasks.itemAt(i)
            if (!task || (!dragging && task.bounceOffset <= 0)) continue
            const left = Math.max(task.floating ? 0 : viewport.x, viewport.x + task.x - viewport.contentX)
            const right = Math.min(task.floating ? width : viewport.x + viewport.width, viewport.x + task.x - viewport.contentX + task.width)
            if (right > left) areas.push({x: left, y: task.y - task.bounceOffset,
                width: right - left, height: task.height + task.bounceOffset})
        }
        return areas
    }
    // Geometry, presentation motion, model membership, and native drag
    // coverage all invalidate the same region. Flush once after bindings settle;
    // animated geometry requests another flush on each changing frame.
    property bool inputUpdatePending: false
    function requestInputUpdate() {
        if (previewMode || inputUpdatePending) return
        inputUpdatePending = true
        Qt.callLater(root.applyInputRegion)
    }
    function applyInputRegion() {
        // Clear before applying so a synchronous invalidation is not lost.
        inputUpdatePending = false
        if (previewMode) return
        const rectangles = currentHoverAreas().map(area =>
            presentation.mapToItem(root, Qt.rect(area.x, area.y, area.width, area.height)))
        if (autoHideEnabled) rectangles.push(Qt.rect(edge.x, edge.y, edge.width, edge.height))
        // Native drag events follow the input region. Accept them anywhere on
        // the surface so targeting matches the in-window drag.
        if (dragControl.handedOff || externalDropControl.acceptedDrag || trashDrag
                || (activeFolderView && !activeFolderView.nativeWindow))
            rectangles.push(Qt.rect(0, 0, width, height))
        const inset = shelf.rearInset
        const polygon = [
            Qt.point(shelf.x + inset, shelf.y),
            Qt.point(shelf.x + shelf.width - inset, shelf.y),
            Qt.point(shelf.x + shelf.width, shelf.y + shelf.height - shelf.lip),
            Qt.point(shelf.x + shelf.width, height),
            Qt.point(shelf.x, height),
            Qt.point(shelf.x, shelf.y + shelf.height - shelf.lip)
        ].map(point => presentation.mapToItem(root, point.x, point.y))
        dockBackend.setInputRegion(dockWindow, rectangles, polygon)
    }
    Component.onCompleted: { applyLayoutInput(); dockBackend.trackDragPointer(dockWindow); requestInputUpdate() }
    onWidthChanged: requestInputUpdate()
    onHeightChanged: requestInputUpdate()
    onLayoutChanged: requestInputUpdate()


    // Our own icon, while KWin draws it as a native drag image.
    DropArea {
        id: ownDrop
        objectName: "taskDropArea"
        anchors.fill: parent
        enabled: dragControl.handedOff
        keys: [dockBackend.taskDragMimeType]
        onEntered: event => {
            event.accept(Qt.MoveAction)
            dragControl.traceDrop("nativeEnter accepted Move", Qt.point(event.x, event.y))
            dragControl.nativeMove(Qt.point(event.x, event.y))
        }
        onPositionChanged: event => {
            event.accept(Qt.MoveAction)
            dragControl.nativeMove(Qt.point(event.x, event.y))
        }
        onExited: dragControl.nativeLeave()
        onDropped: event => { event.accept(Qt.MoveAction); dragControl.nativeDrop(Qt.point(event.x, event.y)) }
    }

    property bool trashDrag: false
    property bool trashHover: false
    DropArea {
        id: appDrop
        objectName: "applicationDropArea"
        anchors.fill: parent
        enabled: !root.dragging && !root.shuttingDown && !root.startingUp
        onEntered: event => {
            externalDropControl.enter(event, presentation.mapFromItem(appDrop, event.x, event.y))
            if (event.hasUrls && dockBackend.canTrashUrls(event.urls)
                    && (event.supportedActions & (Qt.CopyAction | Qt.MoveAction))
                    && event.formats.indexOf("text/xx-kicker-kastatsfavorite-id") < 0
                    && event.formats.indexOf("text/xx-kicker-simplefavorite-id") < 0) {
                trashDrag = true
                hideRequested = false
                requestInputUpdate()
                event.accept(event.supportedActions & Qt.CopyAction ? Qt.CopyAction : Qt.MoveAction)
            }
        }
        onPositionChanged: event => {
            const point = presentation.mapFromItem(appDrop, event.x, event.y)
            trashHover = trashDrag && point.x >= viewport.x + trashIcon.x - viewport.contentX && point.x <= viewport.x + trashIcon.x - viewport.contentX + trashIcon.width
                && point.y >= trashIcon.y && point.y <= trashIcon.y + trashIcon.height
            if (externalDropControl.acceptedDrag && !trashHover) externalDropControl.move(event, point)
            else if (trashDrag) event.accept(event.supportedActions & Qt.CopyAction ? Qt.CopyAction : Qt.MoveAction)
        }
        onExited: { trashDrag = false; trashHover = false; externalDropControl.leave() }
        onContainsDragChanged: externalDropControl.containsDragChanged(containsDrag)
        onDropped: event => {
            const point = presentation.mapFromItem(appDrop, event.x, event.y)
            if (trashDrag && point.x >= viewport.x + trashIcon.x - viewport.contentX && point.x <= viewport.x + trashIcon.x - viewport.contentX + trashIcon.width
                    && point.y >= trashIcon.y && point.y <= trashIcon.y + trashIcon.height) {
                event.accepted = dockBackend.trashUrls(event.urls)
                if (event.accepted) event.accept(event.supportedActions & Qt.CopyAction ? Qt.CopyAction : Qt.MoveAction)
            } else if (externalDropControl.acceptedDrag) externalDropControl.drop(event, point)
            else event.accepted = false
            externalDropControl.leave()
            trashDrag = false; trashHover = false
        }
    }
    ExternalDropController {
        id: externalDropControl
        objectName: "externalDropController"
        backend: dockBackend
        tasks: tasks
        viewport: viewport
        placementGeometry: root.placementGeometry
        surfaceWidth: root.width
        geometryWidth: root.geometry.width
        durationMs: root.partingDurationMs
        hoverDelayMs: dragControl.partingHoverDelayMs
        iconSize: root.layout.iconSize
        iconBottom: root.iconBottom
        dragging: root.dragging
        onPointerMoved: position => { root.pointerX = position.x; root.pointerInside = !root.hideRequested }
        onRevealRequested: { root.hideRequested = false; root.tooltipReady = false }
        onPointerLeft: { root.pointerInside = false; autoHideControl.scheduleHide() }
        onInputInvalidated: root.requestInputUpdate()
    }

    // One transform keeps shelf, icons and reflections moving together.
    // Travel only far enough to clear the resting artwork, avoiding dead time
    // while a whole surface-height slide remains below the screen.
    Item {
        id: presentation
        objectName: "dockPresentation"
        width: root.width
        height: root.height
        y: root.hideOffset / Math.max(1, root.height) * (root.layout.shelfHeight + root.feedbackSettings.bounceHeight)
        scale: root.revealScale
        transformOrigin: Item.Bottom

    Repeater {
        model: closeAnimation.ghosts
        delegate: TaskArtwork {
            required property var modelData
            objectName: "closingTaskArtwork"
            x: modelData.x; y: modelData.y
            width: modelData.size; height: width
            source: modelData.source
            sourceSize: Qt.size(Math.ceil(tuning.artworkSize * root.magnification.scaleSamples[0]),
                Math.ceil(tuning.artworkSize * root.magnification.scaleSamples[0]))
            placeholder: modelData.placeholder
            openProgress: modelData.openProgress
            animateOpening: false
            opacity: 1 - closeAnimation.progress
            z: 2
        }
    }
    Repeater {
        model: closeAnimation.ghosts
        delegate: Item {
            required property var modelData
            x: modelData.x; y: root.iconBottom
            width: modelData.size; height: root.layout.iconBaselineInset - shelf.lip
            clip: true
            opacity: root.layout.reflectionOpacity * (1 - closeAnimation.progress)
            TaskArtwork {
                width: parent.width; height: width
                source: modelData.source
                sourceSize: Qt.size(Math.ceil(tuning.artworkSize * root.magnification.scaleSamples[0]),
                    Math.ceil(tuning.artworkSize * root.magnification.scaleSamples[0]))
                placeholder: modelData.placeholder
                openProgress: modelData.openProgress
                animateOpening: false
                mirrorVertically: true
            }
        }
    }

    Shelf {
        id: shelf
        y: root.height - height
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.shelfWidth
        height: root.layout.shelfDepth
        inset: root.layout.perspectiveInset
        lip: root.layout.shelfLip
        onXChanged: root.requestInputUpdate()
        onWidthChanged: root.requestInputUpdate()
    }

    AppImageArtworkTransition {
        id: appImageTransition
        objectName: "appImageArtworkTransition"
        anchors.fill: parent
        dock: root
        tasks: tasks
        viewport: viewport
        reflectionHeight: root.layout.iconBaselineInset - shelf.lip
    }

    Flickable {
        id: viewport
        objectName: "taskViewport"
        z: 1
        x: (root.width - root.shelfWidth) / 2 + root.layout.padding
        width: Math.max(0, root.shelfWidth - 2 * root.layout.padding)
        height: root.height
        contentWidth: root.geometry.width
        contentHeight: height
        clip: !root.dragging
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        // Wheel scrolling remains available while icon drags own pointer grabs.
        WheelHandler {
            onWheel: event => {
                if (root.dragging) return
                viewport.contentX = Math.max(0, Math.min(viewport.contentWidth - viewport.width,
                    viewport.contentX - (event.angleDelta.y || event.angleDelta.x) / 2))
                event.accepted = true
            }
        }
        flickableDirection: Flickable.HorizontalFlick
        function clampScroll() { contentX = Math.max(0, Math.min(contentX, Math.max(0, contentWidth - width))) }
        onContentWidthChanged: clampScroll()
        onContentXChanged: root.requestInputUpdate()
        onWidthChanged: { clampScroll(); root.requestInputUpdate() }
        Item {
            id: icons
            width: root.geometry.width
            height: root.height
            Repeater {
                id: tasks
                model: dockBackend
                onCountChanged: {
                    externalDropControl.modelChanged()
                }
                onItemAdded: { externalDropControl.modelChanged(); root.delegateRevision++; root.requestInputUpdate(); Qt.callLater(root.synchronizeFeedback) }
                onItemRemoved: { externalDropControl.modelChanged(); root.delegateRevision++; root.requestInputUpdate(); Qt.callLater(root.synchronizeFeedback) }
                delegate: TaskDelegate {
                    dockRoot: root
                    dockDragControl: dragControl
                    dockExternalDropControl: externalDropControl
                    dockCloseAnimation: closeAnimation
                    dockViewport: viewport
                    dockPresentation: presentation
                    dockShelf: shelf
                    dockAppImageTransition: appImageTransition
                }
            }
        }
    }
    TrashItem {
        id: trashIcon
        objectName: "trashIcon"
        parent: icons
        readonly property var placement: root.geometry.items[root.geometry.items.length - 1] || {x: 0, size: root.layout.iconSize}
        x: closeAnimation.active
            ? closeAnimation.trashX + (viewport.x + placement.x - viewport.contentX - closeAnimation.trashX) * closeAnimation.progress - viewport.x + viewport.contentX
            : placement.x
        y: root.iconBottom - height
        width: closeAnimation.active ? closeAnimation.trashSize + (placement.size - closeAnimation.trashSize) * closeAnimation.progress : placement.size
        height: width
        dock: root
        shelfLip: shelf.lip
        onPressedChanged: pressed => root.pressedTask = pressed ? tasks.count : -1
        onMenuRequested: trashMenu.openForIcon()
        onXChanged: root.requestInputUpdate()
        onYChanged: root.requestInputUpdate()
        onWidthChanged: root.requestInputUpdate()
    }
    Item {
        id: trashMenuAnchor
        parent: root
        property rect artworkBounds
        readonly property var observedGeometry: trashMenu.visible
            ? [trashIcon.x, trashIcon.y, trashIcon.width, trashIcon.height,
                viewport.x, viewport.contentX, presentation.y, presentation.scale] : []
        onObservedGeometryChanged: sync()
        function sync() {
            artworkBounds = trashIcon.mapToItem(root, Qt.rect(0, 0, trashIcon.width, trashIcon.height))
        }
        Component.onCompleted: sync()
        x: artworkBounds.x; y: artworkBounds.y
        width: artworkBounds.width; height: artworkBounds.height
    }
    DockMenu {
        id: trashMenu
        objectName: "trashMenu"
        placement: Item { parent: trashMenuAnchor }
        parent: placement
        overlap: 0
        showPointer: true
        pointerTarget: trashMenuAnchor
        property bool holdsDock: false
        function acquireDock() { if (!holdsDock) { holdsDock = true; root.acquireMenuHold() } }
        function releaseDock() { if (holdsDock) { holdsDock = false; root.releaseMenuHold() } }
        function positionAtIcon() { placeAbove(trashMenuAnchor, root.width) }
        function openForIcon() { openWithScale(1) }
        function openForTouch() { openWithScale(2) }
        function openWithScale(scale) {
            menuScale = scale
            acquireDock()
            positionAtIcon()
            open()
            if (!visible) releaseDock()
        }
        onOpened: acquireDock()
        onClosed: releaseDock()
        onHeightChanged: if (visible) positionAtIcon()
        onWidthChanged: if (visible) positionAtIcon()
        Component.onDestruction: releaseDock()
        Connections {
            target: trashMenuAnchor
            function onArtworkBoundsChanged() { if (trashMenu.visible) trashMenu.positionAtIcon() }
        }
        DockMenuItem { text: qsTr("Open"); onTriggered: dockBackend.openTrash() }
        DockMenuItem {
            text: qsTr("Empty Trash…")
            enabled: !root.trashConfirmationPending
            onTriggered: {
                root.trashConfirmationPending = true
                root.pointerInside = false
                dockBackend.trash.requestConfirmation()
            }
        }
    }
    MessageDialog {
        id: trashErrorDialog
        title: qsTr("Trash")
        buttons: MessageDialog.Ok
    }
    Connections {
        target: dockBackend.trash
        function onConfirmationReady(count, size) {
            dockBackend.trash.showConfirmation(count, size, root.dockWindow)
        }
        function onOperationFailed(message) {
            root.trashConfirmationPending = false
            Qt.callLater(root.refreshPointer)
            trashErrorDialog.text = message
            trashErrorDialog.open()
        }
    }
    Item {
        objectName: "taskGroupSeparator"
        opacity: 0.8
        visible: root.groupBoundary !== null
        x: root.groupBoundary ? viewport.x - viewport.contentX
            + root.groupFenceX - width / 2 : 0
        y: root.iconBottom - height + 11
        width: 3
        height: root.layout.iconSize * 0.65
        // Match the slope of the shelf's right-hand perspective edge.
        rotation: -Math.atan2(shelf.rearInset, Math.max(1, shelf.height - shelf.lip)) * 180 / Math.PI
        Rectangle { width: 2; height: parent.height; color: "#616161"; antialiasing: true }
        Rectangle { x: 2; width: 1; height: parent.height; color: "#b8b8b8"; antialiasing: true }
    }
    DockToolTip {
        id: tooltip
        z: 10
        settings: root.tooltipSettings
        visible: !root.hideRequested && root.hideOffset === 0 && root.tooltipSettings.enabled
            && (touchControl.usingTouch ? touchControl.labelsReady : root.tooltipReady) && root.hoveredIndex >= 0
        label: root.hoveredIndex === root.geometry.items.length - 1 ? qsTr("Trash") : root.hoveredTask ? root.hoveredTask.appName : ""
        targetCenter: root.hoveredIndex === root.geometry.items.length - 1
            ? viewport.x + trashIcon.x - viewport.contentX + trashIcon.width / 2
            : root.hoveredTask ? viewport.x + root.hoveredTask.x - viewport.contentX + root.hoveredTask.width / 2 : 0
        targetTop: root.hoveredIndex === root.geometry.items.length - 1 ? trashIcon.y : root.hoveredTask ? root.hoveredTask.y - root.hoveredTask.bounceOffset : 0
    }
    }
}
