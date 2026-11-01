import QtQuick

DockMenu {
    id: menu
    objectName: "appImageSetupMenu"
    required property var dock
    required property var taskItem
    required property Item anchor
    placement: Item { parent: menu.anchor }
    parent: placement
    overlap: 0
    implicitWidth: 340 + 2 * shadowMargin
    showPointer: true
    pointerTarget: anchor
    property bool holdsDock: false
    property bool selecting: false
    readonly property bool ownsSetup: dockBackend.appImageSetupPending
        && dockBackend.appImageSetupKey === taskItem.appKey

    function positionAtIcon() { placeAbove(anchor, dock.width) }
    function syncVisible() {
        if (ownsSetup && dockBackend.appImageSetupVisible) {
            if (!visible) {
                acquireDock() // before placement; see TaskMenu.openForIcon()
                positionAtIcon()
                open()
                if (!visible) releaseDock()
            }
        } else if (visible) close()
    }
    function acquireDock() {
        if (holdsDock) return
        holdsDock = true
        dock.acquireMenuHold()
    }
    function releaseDock() {
        if (!holdsDock) return
        holdsDock = false
        dock.releaseMenuHold()
    }
    Connections {
        target: dockBackend
        function onAppImageSetupChanged() { menu.syncVisible() }
    }
    Connections {
        target: anchor
        function onArtworkBoundsChanged() { if (menu.visible) menu.positionAtIcon() }
    }
    onHeightChanged: if (visible) positionAtIcon()
    Component.onCompleted: Qt.callLater(syncVisible)
    onOpened: {
        selecting = false
        acquireDock()
    }
    onClosed: {
        releaseDock()
        if (ownsSetup && !selecting) dockBackend.cancelAppImageSetup()
    }
    Component.onDestruction: releaseDock()

    DockMenuItem {
        objectName: "appImageSetupHeading"
        text: qsTr("Confirm the Appimage")
        enabled: false
    }
    DockMenuItem {
        text: qsTr("Choose the application that opened:")
        enabled: false
        font.pixelSize: 12
    }
    DockMenuSeparator {}
    Instantiator {
        id: candidates
        model: menu.ownsSetup ? dockBackend.appImageSetupCandidates : []
        delegate: DockMenuItem {
            required property var modelData
            objectName: "appImageSetupCandidate"
            property string candidateKey: modelData.key
            text: modelData.wasRunning ? qsTr("%1 (already running)").arg(modelData.name) : modelData.name
            leftPadding: 38
            Image {
                x: 12
                y: (parent.height - height) / 2
                width: 18; height: 18
                source: modelData.iconUrl
                sourceSize: Qt.size(36, 36)
                smooth: true
            }
            onTriggered: {
                menu.selecting = true
                if (!dockBackend.confirmAppImageSetup(candidateKey)) Qt.callLater(menu.syncVisible)
            }
        }
        onObjectAdded: (index, object) => menu.insertItem(index + 3, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
    DockMenuItem {
        shown: candidates.count === 0
        enabled: false
        text: qsTr("Waiting for an application to open…")
        font.pixelSize: 12
    }
    DockMenuItem {
        shown: dockBackend.appImageSetupError.length > 0
        height: shown ? implicitHeight * 2 : 0
        enabled: false
        text: dockBackend.appImageSetupError
        font.pixelSize: 12
    }
    DockMenuSeparator {}
    DockMenuItem {
        objectName: "appImageSetupCancel"
        text: qsTr("Cancel")
        onTriggered: dockBackend.cancelAppImageSetup()
    }
}
