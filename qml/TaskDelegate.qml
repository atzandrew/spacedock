import QtQuick

Item {
    id: task
    required property var dockRoot
    required property var dockDragControl
    required property var dockExternalDropControl
    required property var dockCloseAnimation
    required property var dockViewport
    required property var dockPresentation
    required property var dockShelf
    required property var dockAppImageTransition
    objectName: "taskDelegate"
    required property int index
    required property string appName
    required property string appKey
    onAppKeyChanged: {
        if (dockRoot.dragging && dockRoot.dragFrom === index && appKey !== dockDragControl.dragAppKey)
            dockRoot.cancelReorder()
    }
    required property bool running
    required property bool pinned
    required property bool launching
    required property bool attention
    required property bool active
    required property bool needsAppImageSetup
    readonly property bool loading: launching && !running && !active
    readonly property bool launchFeedback: bounce.playing && bounce.reason === "launch"
    readonly property real bounceOffset: bounce.offset
    onBounceOffsetChanged: dockRoot.requestInputUpdate()
    readonly property var bounce: dockRoot.bounceFor(appKey)
    onBounceChanged: Qt.callLater(dockRoot.synchronizeFeedback)
    onLoadingChanged: Qt.callLater(dockRoot.synchronizeFeedback)
    // Keep attention edges distinct even if clear/re-request
    // arrives in one event turn; lifecycle replacements coalesce.
    onAttentionChanged: dockRoot.synchronizeFeedback()
    onActiveChanged: Qt.callLater(dockRoot.synchronizeFeedback)
    onRunningChanged: Qt.callLater(dockRoot.synchronizeFeedback)
    Component.onCompleted: {
        Qt.callLater(dockRoot.synchronizeFeedback)
    }
    property real folderOpenProgress: folderView.visible ? 1 : 0
    Behavior on folderOpenProgress {
        NumberAnimation { duration: dockRoot.partingDurationMs; easing.type: Easing.OutCubic }
    }
    readonly property string iconUrl: "image://tasks/" + dockBackend.revision + "/" + index
    // The shared gap progress interpolates compact and parted
    // positions together; no second delegate animation chases it.
    readonly property real visualSlot: dockRoot.dragging ? dockDragControl.slotFor(index)
        : index + (!dockExternalDropControl.committing && dockExternalDropControl.slot >= 0 && index >= dockExternalDropControl.slot ? 1 : 0)
    readonly property var commitStart: dockExternalDropControl.commitPositions[appKey]
    readonly property var closeStart: dockCloseAnimation.positions[appKey]
    opacity: dockExternalDropControl.committing
        ? commitStart ? commitStart.opacity + (1 - commitStart.opacity) * dockExternalDropControl.commitProgress
            : dockExternalDropControl.commitProgress
        : 1
    Behavior on x {
        enabled: !dockRoot.dragging && !dockExternalDropControl.committing && dockExternalDropControl.gapProgress === 1 && dockExternalDropControl.acceptedDrag
        NumberAnimation { duration: dockRoot.partingDurationMs; easing.type: Easing.OutCubic }
    }
    readonly property var placement: {
        const slot = Math.max(0, Math.min(dockRoot.geometry.items.length - 1, visualSlot))
        const lower = Math.floor(slot)
        const a = dockRoot.geometry.items[lower] || {x: 0, size: dockRoot.layout.iconSize}
        const b = dockRoot.geometry.items[Math.ceil(slot)] || a
        const progress = slot - lower
        return {x: a.x + (b.x - a.x) * progress, size: a.size + (b.size - a.size) * progress}
    }
    readonly property bool floating: dockRoot.dragging && dockRoot.dragFrom === index
    readonly property real finalOrigin: (dockRoot.width - Math.min(dockRoot.width, dockRoot.committedGeometry.width + 2 * dockRoot.layout.padding)) / 2 + dockRoot.layout.padding
    readonly property real commitX: commitStart
        ? commitStart.x + (finalOrigin + placement.x - dockViewport.contentX - commitStart.x) * dockExternalDropControl.commitProgress
            - dockViewport.x + dockViewport.contentX
        : finalOrigin + placement.x - dockViewport.x
    x: floating ? dockRoot.floatingLeft - dockViewport.x + dockViewport.contentX
        : dockExternalDropControl.committing ? commitX
        : dockCloseAnimation.active && closeStart
            ? closeStart.x + (dockViewport.x + placement.x - dockViewport.contentX - closeStart.x) * dockCloseAnimation.progress - dockViewport.x + dockViewport.contentX
            : placement.x
    z: floating ? 1 : 0
    onXChanged: dockRoot.requestInputUpdate()
    onYChanged: dockRoot.requestInputUpdate()
    y: floating ? dockRoot.floatingTop : dockRoot.iconBottom - height
    width: floating ? dockRoot.floatingSize
        : dockExternalDropControl.committing && commitStart
            ? commitStart.size + (placement.size - commitStart.size) * dockExternalDropControl.commitProgress
            : dockCloseAnimation.active && closeStart
                ? closeStart.size + (placement.size - closeStart.size) * dockCloseAnimation.progress
            : placement.size
    height: width
    TaskArtwork {
        id: icon
        objectName: "taskArtwork"
        width: task.width
        height: task.height
        y: task.floating ? 0 : -task.bounceOffset
        visible: !(task.floating && dockDragControl.handedOff) && !dockAppImageTransition.covers(task)
        // Qt Quick applies the window's device pixel ratio to image requests.
        sourceSize: Qt.size(Math.ceil(tuning.artworkSize * dockRoot.magnification.scaleSamples[0]),
            Math.ceil(tuning.artworkSize * dockRoot.magnification.scaleSamples[0]))
        source: task.iconUrl
        grayscaleSource: "image://tasks/gray/" + dockBackend.revision + "/" + task.index
        grayscaleProgress: task.folderOpenProgress
        opacity: (task.floating && dockDragControl.removing ? 1 - dockDragControl.removalProgress
            : click.pressed && !task.floating ? 0.55 : 1) * (1 - 0.2 * task.folderOpenProgress)
        placeholder: task.needsAppImageSetup
        opened: task.needsAppImageSetup && dockBackend.appImageSetupPending
            && dockBackend.appImageSetupKey === task.appKey
        duration: dockRoot.partingDurationMs
    }
    Rectangle {
        objectName: "removalCue"
        visible: task.floating && dockDragControl.removalReady
        anchors.horizontalCenter: parent.horizontalCenter
        y: -height - 6
        width: removeLabel.implicitWidth + 16
        height: removeLabel.implicitHeight + 8
        radius: 5
        color: "#dd242424"
        Text { id: removeLabel; anchors.centerIn: parent; text: qsTr("Remove"); color: "white" }
    }
    readonly property real appImageOpenProgress: icon.openProgress
    // Reuse the themed icon; no reference artwork or shader needed.
    Item {
        visible: !task.floating && !dockAppImageTransition.covers(task)
        x: task.needsAppImageSetup ? -task.width * 0.25 : 0
        y: task.height
        width: task.width * (task.needsAppImageSetup ? 1.5 : 1)
        height: dockRoot.layout.iconBaselineInset - dockShelf.lip
        clip: true
        opacity: dockRoot.layout.reflectionOpacity
        TaskArtwork {
            x: task.needsAppImageSetup ? task.width * 0.25 : 0
            width: task.width
            height: task.height
            y: task.bounceOffset
            sourceSize: icon.sourceSize
            source: task.iconUrl
            grayscaleSource: "image://tasks/gray/" + dockBackend.revision + "/" + task.index
            grayscaleProgress: task.folderOpenProgress
            mirrorVertically: true
            opacity: (click.pressed && !task.floating ? 0.55 : 1) * (1 - 0.2 * task.folderOpenProgress)
            placeholder: task.needsAppImageSetup
            openProgress: icon.openProgress
            animateOpening: false
        }
    }
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: task.height + dockRoot.layout.iconBaselineInset - dockShelf.lip - 7
        width: 12; height: 6; radius: 3
        gradient: Gradient {
            GradientStop { position: 0; color: "#00c4efff" }
            GradientStop { position: 0.5; color: "#ddc4efff" }
            GradientStop { position: 1; color: "#00c4efff" }
        }
        visible: !task.floating && (task.running || task.loading || task.launchFeedback || task.attention) && dockRoot.layout.showIndicators
        Rectangle {
            anchors.centerIn: parent
            width: 5; height: 3; radius: 1.5
            color: task.attention && !task.active ? "#ffd58a"
                : task.loading || (task.launchFeedback && !task.running) ? "#9bcaff" : "#f2faff"
        }
    }
    MouseArea {
        id: click
        objectName: "taskMouseArea"
        width: task.width
        height: task.height + task.bounceOffset
        y: -task.bounceOffset
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true
        property point pressPosition: Qt.point(0, 0)
        property real grabFractionX: 0
        property real grabFractionY: 0
        property bool reordered: false
        onPressed: mouse => {
            if (dockRoot.settling) dockRoot.cancelReorder()
            pressPosition = mapToItem(dockRoot, mouse.x, mouse.y)
            const position = dockPresentation.mapFromItem(dockRoot, pressPosition.x, pressPosition.y)
            const artwork = task.mapToItem(dockPresentation, 0, -task.bounceOffset)
            grabFractionX = (position.x - artwork.x) / task.width
            grabFractionY = (position.y - artwork.y) / task.height
            reordered = false
        }
        onPositionChanged: mouse => {
            if (!(pressedButtons & Qt.LeftButton)) return
            // Cancellation consumes this physical press. A later
            // move must never relift an icon under the same grab.
            if (dockRoot.settling || (!dockRoot.dragging && reordered)) return
            // mouse coordinates belong to the moving delegate;
            // convert once to stable dock-window coordinates.
            const windowPosition = mapToItem(dockRoot, mouse.x, mouse.y)
            if (!dockRoot.dragging && Math.hypot(windowPosition.x - pressPosition.x,
                    windowPosition.y - pressPosition.y) >= Qt.styleHints.startDragDistance) {
                dockDragControl.begin(task, windowPosition, grabFractionX, grabFractionY)
                dockRoot.forceActiveFocus()
                reordered = true
            }
            dockDragControl.move(windowPosition)
        }
        onReleased: mouse => dockDragControl.release(mapToItem(dockRoot, mouse.x, mouse.y))
        // A native handoff ends the pointer grab; that is not a cancel.
        onCanceled: {
            dockDragControl.traceDrop("MouseArea canceled")
            if (!dockDragControl.handedOff && !dockRoot.settling) dockRoot.cancelReorder()
        }
        onPressedChanged: dockRoot.pressedTask = pressed ? task.index : -1
        onClicked: mouse => {
            if (reordered) return
            if (mouse.button === Qt.RightButton) menu.openForIcon()
            else {
                if (dockBackend.folderPath(task.index)) folderView.openForIcon()
                else {
                    if (!task.running && !task.active) bounce.triggerLaunch()
                    dockBackend.activate(task.index, tuning.bringAllWindows)
                }
            }
        }
    }
    // Keep the native popup's parent-control rectangle equal
    // to the rendered artwork, rather than the full dock surface.
    Item {
        id: menuAnchor
        objectName: "dockMenuAnchor"
        parent: dockRoot
        property rect artworkBounds
        // Hidden launchers need geometry only when opening a popup. Running
        // tasks also publish a debounced minimize target to the window manager.
        // Observe transforms explicitly because mapToItem does not track them.
        readonly property var observedGeometry: task.running || dockRoot.openMenus > 0
            ? [task.x, task.y, task.width, task.height, task.bounceOffset,
                dockViewport.x, dockViewport.y, dockViewport.contentX,
                dockPresentation.x, dockPresentation.y, dockPresentation.scale] : []
        onObservedGeometryChanged: scheduleRefresh()
        property bool refreshPending: false
        function scheduleRefresh() {
            if (refreshPending) return
            refreshPending = true
            Qt.callLater(refresh)
        }
        // Menu placement always samples current artwork, including launchers
        // whose hidden anchors have not been observing geometry.
        function sync() {
            if (!task || !dockRoot) return
            artworkBounds = task.mapToItem(dockRoot, Qt.rect(0, -task.bounceOffset, task.width, task.height))
        }
        function refresh() {
            refreshPending = false
            // The root-parented anchor can outlive its delegate during removal.
            if (!task || !dockRoot) return
            if (dockRoot.openMenus > 0) sync()
            if (task.running) geometryPublish.restart()
        }
        Component.onCompleted: sync()
        x: artworkBounds.x
        y: artworkBounds.y
        width: artworkBounds.width
        height: artworkBounds.height
    }
    function syncMenuAnchor() { menuAnchor.sync() }
    Timer {
        id: geometryPublish
        interval: 50
        onTriggered: if (task.running) {
            menuAnchor.sync()
            dockBackend.publishIconGeometry(task.index, menuAnchor)
        }
    }
    Connections {
        target: dockBackend
        function onRevisionChanged() { if (task.running) geometryPublish.restart() }
    }
    onIndexChanged: {
        dockRoot.delegateRevision++
        // Repeater updates its item lookup after changing the
        // delegate indexes during a model row move.
        Qt.callLater(dockRoot.refreshDelegateRevision)
        geometryPublish.restart()
    }
    FolderView {
        id: folderView
        dock: dockRoot
        taskItem: task
        anchor: menuAnchor
    }
    TaskMenu {
        id: menu
        dock: dockRoot
        taskItem: task
        anchor: menuAnchor
        targetFolderView: folderView
    }
    AppImageSetupMenu {
        dock: dockRoot
        taskItem: task
        anchor: menuAnchor
    }
}
