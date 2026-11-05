import QtQuick

// Owns task actions, artwork-relative placement, and one root-taskMenuRoot hold.
DockMenu {
    required property var dock
    required property var taskItem
    required property Item anchor
    property var targetFolderView: null
    id: taskMenuRoot
    objectName: "taskMenu"
    placement: Item { objectName: "dockMenuPlacement"; parent: taskMenuRoot.anchor }
    parent: placement
    overlap: 0
    showPointer: true
    pointerTarget: anchor
    property var appEntries: []
    property bool isFolder: false
    function openForIcon() {
        appEntries = dockBackend.contextEntries(taskItem.index)
        isFolder = !!dockBackend.folderPath(taskItem.index)
        if (isFolder && targetFolderView) targetFolderView.loadPreferences()
        // Hold first: the popup's input grab reports the pointer leaving,
        // which must not start moving icons before the taskMenuRoot is placed.
        acquireDock()
        positionAtIcon()
        open()
        if (!visible) releaseDock()
    }
    function positionAtIcon() { placeAbove(anchor, dock.width) }
    Connections {
        target: anchor
        function onArtworkBoundsChanged() { if (taskMenuRoot.visible) taskMenuRoot.positionAtIcon() }
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
        model: taskMenuRoot.appEntries.filter(entry => entry.section === "actions")
        delegate: DockMenuItem {
            required property var modelData
            text: modelData.text
            onTriggered: dockBackend.triggerContextAction(modelData.token)
        }
        onObjectAdded: (index, object) => taskMenuRoot.insertItem(index, object)
        onObjectRemoved: (index, object) => taskMenuRoot.removeItem(object)
    }
    DockMenu {
        id: recentMenu
        objectName: "dockRecentMenu"
        title: qsTr("Recent Files")
        enabled: recentItems.count > 0
        Instantiator {
            id: recentItems
            model: taskMenuRoot.appEntries.filter(entry => entry.section === "recent")
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
        visible: taskMenuRoot.appEntries.length > 0
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
        shown: taskItem.running && !taskMenuRoot.appEntries.some(entry => entry.section === "actions")
        onTriggered: dockBackend.taskAction(taskItem.index, "new")
    }
    DockMenuSeparator {}
    DockMenuItem {
        text: qsTr("Sort by")
        enabled: false
        shown: taskMenuRoot.isFolder
    }
    DockMenuItem {
        objectName: "folderSort_name"
        text: qsTr("Name")
        shown: taskMenuRoot.isFolder
        checked: !!taskMenuRoot.targetFolderView && taskMenuRoot.targetFolderView.sortMode === "name"
        onTriggered: if (taskMenuRoot.targetFolderView) taskMenuRoot.targetFolderView.setPreference("sort", "name")
    }
    DockMenuItem {
        objectName: "folderSort_dateAdded"
        text: qsTr("Date added")
        shown: taskMenuRoot.isFolder
        checked: !!taskMenuRoot.targetFolderView && taskMenuRoot.targetFolderView.sortMode === "dateAdded"
        onTriggered: if (taskMenuRoot.targetFolderView) taskMenuRoot.targetFolderView.setPreference("sort", "dateAdded")
    }
    DockMenuItem {
        objectName: "folderSort_dateModified"
        text: qsTr("Date modified")
        shown: taskMenuRoot.isFolder
        checked: !!taskMenuRoot.targetFolderView && taskMenuRoot.targetFolderView.sortMode === "dateModified"
        onTriggered: if (taskMenuRoot.targetFolderView) taskMenuRoot.targetFolderView.setPreference("sort", "dateModified")
    }
    DockMenuItem {
        objectName: "folderSort_dateCreated"
        text: qsTr("Date created")
        shown: taskMenuRoot.isFolder
        checked: !!taskMenuRoot.targetFolderView && taskMenuRoot.targetFolderView.sortMode === "dateCreated"
        onTriggered: if (taskMenuRoot.targetFolderView) taskMenuRoot.targetFolderView.setPreference("sort", "dateCreated")
    }
    DockMenuItem {
        objectName: "folderSort_type"
        text: qsTr("File type")
        shown: taskMenuRoot.isFolder
        checked: !!taskMenuRoot.targetFolderView && taskMenuRoot.targetFolderView.sortMode === "type"
        onTriggered: if (taskMenuRoot.targetFolderView) taskMenuRoot.targetFolderView.setPreference("sort", "type")
    }
    DockMenuSeparator { visible: taskMenuRoot.isFolder; height: visible ? implicitHeight : 0 }
    DockMenuItem {
        text: qsTr("View content as")
        enabled: false
        shown: taskMenuRoot.isFolder
    }
    DockMenuItem {
        objectName: "folderViewFan"
        text: qsTr("Fan")
        shown: taskMenuRoot.isFolder
        checked: !!taskMenuRoot.targetFolderView && taskMenuRoot.targetFolderView.viewMode === "fan"
        onTriggered: if (taskMenuRoot.targetFolderView) taskMenuRoot.targetFolderView.setPreference("view", "fan")
    }
    DockMenuItem {
        objectName: "folderViewGrid"
        text: qsTr("Grid")
        shown: taskMenuRoot.isFolder
        checked: !!taskMenuRoot.targetFolderView && taskMenuRoot.targetFolderView.viewMode === "grid"
        onTriggered: if (taskMenuRoot.targetFolderView) taskMenuRoot.targetFolderView.setPreference("view", "grid")
    }
    DockMenuItem {
        objectName: "folderViewList"
        text: qsTr("List")
        shown: taskMenuRoot.isFolder
        checked: !!taskMenuRoot.targetFolderView && taskMenuRoot.targetFolderView.viewMode === "list"
        onTriggered: if (taskMenuRoot.targetFolderView) taskMenuRoot.targetFolderView.setPreference("view", "list")
    }
    DockMenuSeparator {
        visible: taskMenuRoot.isFolder
        height: taskMenuRoot.isFolder ? implicitHeight : 0
    }
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
    DockMenuSeparator {
        visible: taskItem.running
        height: taskItem.running ? implicitHeight : 0
    }
    DockMenuItem {
        text: qsTr("Close Windows")
        shown: taskItem.running
        onTriggered: dockBackend.taskAction(taskItem.index, "close")
    }

}
