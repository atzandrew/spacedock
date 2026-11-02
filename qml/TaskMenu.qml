import QtQuick

// Owns task actions, artwork-relative placement, and one root-menu hold.
DockMenu {
    required property var dock
    required property var taskItem
    required property Item anchor
    id: menu
    objectName: "taskMenu"
    placement: Item { objectName: "dockMenuPlacement"; parent: menu.anchor }
    parent: placement
    overlap: 0
    showPointer: true
    pointerTarget: anchor
    property var appEntries: []
    function openForIcon() {
        appEntries = dockBackend.contextEntries(taskItem.index)
        // Hold first: the popup's input grab reports the pointer leaving,
        // which must not start moving icons before the menu is placed.
        acquireDock()
        positionAtIcon()
        open()
        if (!visible) releaseDock()
    }
    function positionAtIcon() { placeAbove(anchor, dock.width) }
    Connections {
        target: anchor
        function onArtworkBoundsChanged() { if (menu.visible) menu.positionAtIcon() }
    }
    onHeightChanged: if (visible) positionAtIcon()
    onWidthChanged: if (visible) positionAtIcon()
    property bool holdsDock: false
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
    onOpened: {
        acquireDock()
    }
    onClosed: releaseDock()
    Component.onDestruction: releaseDock()
    Instantiator {
        model: menu.appEntries.filter(entry => entry.section === "actions")
        delegate: DockMenuItem {
            required property var modelData
            text: modelData.text
            onTriggered: dockBackend.triggerContextAction(modelData.token)
        }
        onObjectAdded: (index, object) => menu.insertItem(index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
    DockMenu {
        id: recentMenu
        objectName: "dockRecentMenu"
        title: qsTr("Recent Files")
        enabled: recentItems.count > 0
        Instantiator {
            id: recentItems
            model: menu.appEntries.filter(entry => entry.section === "recent")
            delegate: DockMenuItem {
                required property var modelData
                text: modelData.text
                onTriggered: dockBackend.triggerContextAction(modelData.token)
            }
            onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => recentMenu.removeItem(object)
        }
    }
    DockMenuSeparator {
        visible: menu.appEntries.length > 0
        height: visible ? implicitHeight : 0
    }
    DockMenuItem {
        text: taskItem.needsAppImageSetup ? qsTr("Add AppImage to Dock") : taskItem.running ? qsTr("Show") : qsTr("Open")
        onTriggered: {
            const folder = dockBackend.folderPath(taskItem.index)
            if (folder) dockBackend.openFolderPath(folder)
            else dockBackend.activate(taskItem.index, tuning.bringAllWindows)
        }
    }
    DockMenuItem {
        text: qsTr("Choose Running Application…")
        shown: taskItem.needsAppImageSetup
        onTriggered: dockBackend.showAppImageSetup(taskItem.index)
    }
    DockMenuItem {
        text: qsTr("Open New Window")
        shown: taskItem.running && !menu.appEntries.some(entry => entry.section === "actions")
        onTriggered: dockBackend.taskAction(taskItem.index, "new")
    }
    DockMenuSeparator {}
    DockMenu {
        objectName: "dockOptionsMenu"
        title: qsTr("Options")
        DockMenuItem {
            text: taskItem.pinned ? qsTr("Remove from Dock") : qsTr("Keep in Dock")
            onTriggered: dockBackend.togglePin(taskItem.index)
        }
        DockMenuItem {
            text: qsTr("Edit Application…")
            shown: taskItem.pinned && !taskItem.needsAppImageSetup && !dockBackend.folderPath(taskItem.index)
            onTriggered: dock.editApplication(taskItem.index)
        }
        DockMenuItem {
            text: qsTr("Automatically Hide Dock")
            checkable: true
            checked: dock.autoHideEnabled
            onTriggered: tuning.setAutoHideEnabled(!dock.autoHideEnabled)
        }
        DockMenuItem {
            text: qsTr("Dock Settings…")
            onTriggered: dock.openSettings()
        }
    }
    DockMenuItem {
        text: qsTr("Close Windows")
        shown: taskItem.running
        onTriggered: dockBackend.taskAction(taskItem.index, "close")
    }

}
