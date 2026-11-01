import QtQuick

// Lives in the dock presentation, surviving replacement of model delegates.
Item {
    id: transition
    required property var dock
    required property var tasks
    required property Item viewport
    property real reflectionHeight: 0
    property bool active: false
    property string originalKey: ""
    property var targetKeys: []
    property int destination: -1
    property rect originalBounds: Qt.rect(0, 0, 0, 0)
    property real progress: 0
    property real opening: 0
    visible: active
    z: 100

    function covers(task) {
        return active && (task.appKey === originalKey || targetKeys.indexOf(task.appKey) >= 0)
    }
    function bounds(task) {
        const geometry = [task.x, task.y, task.width, task.height, task.bounceOffset, dock.geometry, viewport.x, viewport.contentX]
        return task.mapToItem(transition, Qt.rect(0, -task.bounceOffset, task.width, task.height))
    }
    function cancel() {
        active = false
        fade.stop()
        openBox.stop()
        settling.stop()
    }
    function begin(fromKey, toKey, stableKey, targetIndex) {
        cancel()
        for (let i = 0; i < tasks.count; ++i) {
            const task = tasks.itemAt(i)
            if (!task || task.appKey !== fromKey) continue
            originalBounds = bounds(task)
            opening = task.appImageOpenProgress
            originalKey = fromKey
            targetKeys = [toKey, stableKey]
            destination = targetIndex === undefined ? -1 : targetIndex
            progress = 0
            settling.restart()
            if (opening < 1) { openBox.from = opening; openBox.start() }
            active = true
            return
        }
    }
    readonly property var targetTask: {
        const revision = dock.delegateRevision
        if (!active) return null
        for (let i = 0; i < tasks.count; ++i) {
            const task = tasks.itemAt(i)
            if (task && !task.needsAppImageSetup && targetKeys.indexOf(task.appKey) >= 0) return task
        }
        return null
    }
    readonly property rect targetBounds: targetTask ? bounds(targetTask) : originalBounds
    readonly property bool placed: targetTask !== null && targetTask.pinned
    onPlacedChanged: tryFade()
    // Once the replacement exists, keep the placeholder attached to its
    // current geometry, including layout changes and launch bounce.
    readonly property rect drawnBounds: targetBounds
    function tryFade() {
        if (active && placed && incoming.ready && !openBox.running && !settling.running && !fade.running) fade.start()
    }
    Timer { id: settling; interval: 60; onTriggered: transition.tryFade() }
    NumberAnimation {
        id: openBox
        target: transition; property: "opening"
        to: 1; duration: transition.dock.partingDurationMs
        easing.type: Easing.OutCubic
        onFinished: transition.tryFade()
    }
    NumberAnimation {
        id: fade
        target: transition; property: "progress"
        from: 0; to: 1; duration: transition.dock.partingDurationMs
        easing.type: Easing.InOutCubic
        onFinished: transition.active = false
    }
    Connections {
        target: dockBackend
        function onAppImageReplacementPrepared(fromKey, toKey, stableKey, destination) { transition.begin(fromKey, toKey, stableKey, destination) }
        function onAppImageReplacementAborted(fromKey) { if (transition.originalKey === fromKey) transition.cancel() }
        function onTaskStructureChanged() { if (transition.active && !fade.running) settling.restart() }
    }
    TaskArtwork {
        objectName: "appImageTransitionPlaceholder"
        x: transition.drawnBounds.x; y: transition.drawnBounds.y
        width: transition.drawnBounds.width; height: transition.drawnBounds.height
        sourceSize: Qt.size(256, 256)
        placeholder: true
        animateOpening: false
        openProgress: transition.opening
        opacity: 1 - transition.progress
    }
    TaskArtwork {
        id: incoming
        objectName: "appImageIncomingArtwork"
        x: transition.drawnBounds.x; y: transition.drawnBounds.y
        width: transition.drawnBounds.width; height: transition.drawnBounds.height
        sourceSize: Qt.size(256, 256)
        source: transition.targetTask ? transition.targetTask.iconUrl : ""
        opacity: ready ? transition.progress : 0
        onReadyChanged: transition.tryFade()
    }
    Item {
        x: transition.drawnBounds.x - transition.drawnBounds.width * 0.25; y: transition.dock.iconBottom
        width: transition.drawnBounds.width * 1.5; height: transition.reflectionHeight
        clip: true
        opacity: transition.dock.layout.reflectionOpacity
        TaskArtwork {
            x: transition.drawnBounds.width * 0.25
            width: transition.drawnBounds.width; height: transition.drawnBounds.height
            placeholder: true; mirrorVertically: true
            animateOpening: false
            openProgress: transition.opening
            opacity: 1 - transition.progress
        }
        TaskArtwork {
            x: transition.drawnBounds.width * 0.25
            width: transition.drawnBounds.width; height: transition.drawnBounds.height
            source: incoming.source; mirrorVertically: true
            opacity: incoming.opacity
        }
    }
}
