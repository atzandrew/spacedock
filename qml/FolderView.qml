import QtQuick
import QtQuick.Controls
import QtQuick.Window

// The folder uses the same painted panel, pointer and native popup placement as menus.
DockMenu {
    id: view
    objectName: "folderView"
    required property var dock
    required property var taskItem
    required property Item anchor
    placement: Item { parent: view.anchor }
    parent: placement
    showPointer: viewMode !== "fan"
    background.visible: viewMode !== "fan"
    topPadding: viewMode === "fan" ? 0 : 8 + shadowMargin
    bottomPadding: viewMode === "fan" ? 0 : 8 + pointerHeight + shadowMargin
    leftPadding: viewMode === "fan" ? 0 : 2.5 + shadowMargin
    rightPadding: viewMode === "fan" ? 0 : 2.5 + shadowMargin
    pointerTarget: anchor
    // On the screen-sized dock surface, keep the popup mapped while it closes.
    // A short preview window still needs a native popup to fit the grid.
    nativeWindow: false
    onVisibleChanged: {
        if (visible) dock.activeFolderView = view
        else if (dock.activeFolderView === view) dock.activeFolderView = null
    }
    // The popup stays full-sized; its artwork grows from the dock icon.
    property real openingProgress: 1
    property real closingProgress: 1
    readonly property real visualProgress: closing ? closingProgress : openingProgress
    property real openingOriginX: 0
    readonly property real openingOriginY: height + anchor.height / 2 - 5
    pointerX: !visible ? width / 2 : visualProgress < 1 ? openingOriginX
        : background.mapFromItem(anchor, anchor.width / 2, 0).x
    panelScale: viewMode === "grid" ? visualProgress : 1
    background.opacity: viewMode === "list" ? visualProgress : 1
    panelScaleOriginX: openingOriginX
    panelScaleOriginY: openingOriginY
    readonly property int closeDuration: Math.max(1, Math.round((tuning.autoHide.slideDurationMs ?? 400) * 0.75 * (viewMode === "fan" ? 1.15 : 1)))
    readonly property int openDuration: Math.max(1, Math.round(closeDuration * 0.7))
    exit: Transition {
        NumberAnimation {
            target: view
            property: "closingProgress"
            to: view.viewMode === "list" ? 0 : 0.08
            duration: view.closeDuration
            easing.type: Easing.InCubic
        }
    }
    NumberAnimation {
        id: openingAnimation
        objectName: "folderOpeningAnimation"
        target: view
        property: "openingProgress"
        from: view.viewMode === "list" ? 0 : 0.08; to: 1
        duration: view.openDuration
        easing.type: Easing.OutCubic
    }
    property string folder: ""
    property var entries: []
    property string sortMode: "name"
    property string viewMode: "grid"
    function loadPreferences() {
        const path = dockBackend.folderPath(taskItem.index)
        if (!path) return
        viewMode = dockBackend.folderPreference(path, "view")
        sortMode = dockBackend.folderPreference(path, "sort")
    }
    function setPreference(kind, value) {
        const path = dockBackend.folderPath(taskItem.index)
        if (kind === "view") viewMode = value
        else sortMode = value
        if (path) dockBackend.setFolderPreference(path, kind, value)
    }
    property bool holdsDock: false
    property bool closing: false
    readonly property var sortedEntries: {
        const result = entries.slice()
        if (sortMode === "name") return result
        const key = sortMode === "type" ? "type" : sortMode
        return result.sort((a, b) => {
            const av = a[key], bv = b[key]
            if (typeof av === "number" && typeof bv === "number") return av - bv
            return String(av || "").localeCompare(String(bv || ""), undefined, {numeric: true, sensitivity: "base"})
        })
    }
    function openForIcon() {
        // Popup.Item can pass the dismissing pointer release to the dock icon
        // beneath it. Treat that as the close gesture, never as a reopen.
        if (closing) return
        if (visible) { close(); return }
        loadPreferences()
        openingAnimation.stop()
        closing = false
        closingProgress = 1
        openingProgress = viewMode === "list" ? 0 : 0.08
        folder = dockBackend.folderPath(taskItem.index)
        entries = dockBackend.folderContents(folder)
        if (!holdsDock) { holdsDock = true; dock.acquireMenuHold() }
        nativeWindow = anchor.y < height - 5
        placeAbove(anchor, dock.width)
        if (viewMode === "fan") {
            const center = anchor.x + anchor.width / 2
            const left = Math.max(4, Math.min(dock.width - width - 4, center - width + 110))
            placement.x = left - anchor.x
        }
        openingOriginX = anchor.width / 2 - placement.x
        updatePopupType()
        open()
        if (visible) openingAnimation.restart()
        else releaseDock()
    }
    function releaseDock() {
        if (holdsDock) { holdsDock = false; dock.releaseMenuHold() }
    }
    onAboutToHide: {
        // Give close its own scale value, seeded from the currently visible size.
        closingProgress = openingProgress
        closing = true
        // Keep the arrow and its painted texture stable as the panel starts shrinking.
        if (viewMode !== "fan") openingOriginX = pointerX
        openingAnimation.stop()
        wheelAnimation.stop()
        grid.cancelFlick()
        folderOptions.close()
    }
    onClosed: { openingAnimation.stop(); openingProgress = 1; closingProgress = 1; closing = false; releaseDock() }
    Connections {
        target: dockBackend
        function onFolderItemDragFinished() {
            if (view.visible) view.entries = dockBackend.folderContents(view.folder)
        }
    }
    Component.onDestruction: {
        if (dock.activeFolderView === view) dock.activeFolderView = null
        releaseDock()
    }
    readonly property var fanEntries: {
        const shown = sortedEntries.slice(0, 11)
        const remaining = sortedEntries.length - shown.length
        if (remaining > 0) shown.push({name: qsTr("%n More in %1", "", remaining).arg(dockBackend.defaultFileManagerName()), openFolder: true})
        return shown
    }
    // The production dock spans the screen; previews may need a native popup
    // above their shorter window, so measure the anchor in screen coordinates.
    readonly property real maximumListHeight: {
        // Only list views use it; skip the per-frame anchor mapping otherwise.
        if (viewMode !== "list") return 110
        const screen = dock.Window.window ? dock.Window.window.screen : null
        if (!screen || dock.height >= screen.height) return Math.max(110, anchor.y + 5)
        return Math.max(110, anchor.mapToGlobal(0, 0).y - screen.virtualY + 5)
    }
    implicitWidth: viewMode === "fan" ? Math.min(380, dock.width - 8) : viewMode === "list" ? Math.min(300, dock.width - 8) : Math.min(Math.max(220, dock.width - 8), 620)
    implicitHeight: viewMode === "fan" ? Math.max(64, Math.min(anchor.y < 150 ? 500 : Math.max(64, anchor.y - 10), fanEntries.length * 76 + 12)) : Math.min(viewMode === "list" ? maximumListHeight : 500, Math.max(110, Math.ceil((entries.length + (viewMode === "list" ? 0 : 1)) / grid.columns) * grid.cellHeight + topPadding + bottomPadding + 36 + (viewMode === "list" ? 50 : 0)))
    contentItem: Item {
        id: folderContent
        objectName: "folderContent"
        opacity: view.viewMode === "list" ? view.visualProgress : 1
        transform: Scale {
            origin.x: view.openingOriginX - folderContent.x
            origin.y: view.openingOriginY - folderContent.y
            xScale: view.viewMode === "grid" ? view.visualProgress : 1
            yScale: view.viewMode === "grid" ? view.visualProgress : 1
        }
        implicitWidth: 400
        implicitHeight: 200
        Text {
            id: heading
            visible: view.viewMode !== "fan"
            width: parent.width
            height: 28
            text: taskItem.appName
            color: "white"
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
        Rectangle {
            objectName: "folderScrollDivider"
            anchors { left: parent.left; right: parent.right; top: heading.bottom }
            height: 1
            color: "#3dffffff"
            visible: view.viewMode !== "fan" && grid.contentY > grid.originY + 0.5
            z: 1
        }
        Item {
            id: fan
            objectName: "folderFan"
            anchors.fill: parent
            visible: view.viewMode === "fan"
            readonly property real step: Math.min(76, Math.max(1, (height - 64) / Math.max(1, view.fanEntries.length - 1)))
            // The popup is positioned relative to the folder anchor. Use the
            // captured center directly so clamping the popup at a screen edge
            // cannot shift the stack away from the icon.
            readonly property real iconCenter: view.openingOriginX - folderContent.x
            Repeater {
                objectName: "fanRepeater"
                model: view.fanEntries
                delegate: Item {
                    required property var modelData
                    required property int index
                    objectName: modelData.openFolder ? "fanOpenInFileManager" : "fanEntry"
                    readonly property real progress: Math.max(0, Math.min(1, view.visualProgress * 1.5 - index / Math.max(1, view.fanEntries.length) * 0.5))
                    readonly property real arc: 70 * Math.pow(index / Math.max(1, view.fanEntries.length - 1), 2)
                    readonly property real tilt: 10 * index / Math.max(1, view.fanEntries.length - 1) * progress
                    width: fan.width
                    height: 64
                    x: 0
                    y: fan.height - height - index * fan.step * progress
                    opacity: progress
                    z: -index
                    Image {
                        objectName: "fanFileIcon"
                        rotation: parent.tilt
                        transformOrigin: Item.Left
                        x: fan.iconCenter - width / 2 + parent.arc * parent.progress
                        anchors.verticalCenter: parent.verticalCenter
                        width: 60; height: 60
                        sourceSize: Qt.size(64, 64); cache: false
                        source: !view.visible ? "" : modelData.openFolder ? "image://tasks/theme/exit-symbolic" : "image://tasks/fileicon/" + modelData.icon
                        visible: modelData.openFolder || fanThumbnail.status !== Image.Ready || fanThumbnail.implicitWidth <= 1
                        fillMode: Image.PreserveAspectFit
                    }
                    Image {
                        id: fanThumbnail
                        objectName: "fanThumbnail"
                        rotation: parent.tilt
                        transformOrigin: Item.Left
                        x: fan.iconCenter - width / 2 + parent.arc * parent.progress
                        anchors.verticalCenter: parent.verticalCenter
                        width: 60; height: 60
                        sourceSize: Qt.size(64, 64); asynchronous: true; cache: false
                        source: view.visible && !modelData.openFolder ? "image://folderthumbnails/" + modelData.icon : ""
                        visible: status === Image.Ready && implicitWidth > 1
                        fillMode: Image.PreserveAspectFit
                    }
                    Rectangle {
                        rotation: parent.tilt
                        transformOrigin: Item.Right
                        anchors.right: parent.left
                        anchors.rightMargin: -(fan.iconCenter - 38 + parent.arc * parent.progress)
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(label.implicitWidth + 20, fan.iconCenter - 44)
                        height: 32; radius: 6
                        color: "#df20272d"; border.color: "#80ffffff"; border.width: 1
                        Text {
                            id: label
                            anchors.fill: parent; anchors.margins: 6
                            text: modelData.name; color: "white"
                            verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        property point pressPoint
                        property bool dragStarted: false
                        onPressed: event => { pressPoint = Qt.point(event.x, event.y); dragStarted = false }
                        onPositionChanged: event => {
                            if (modelData.openFolder || !(pressedButtons & Qt.LeftButton) || dragStarted) return
                            if (Math.hypot(event.x - pressPoint.x, event.y - pressPoint.y) >= Qt.styleHints.startDragDistance) {
                                dragStarted = true
                                dockBackend.startFolderItemDrag(view.popupWindow, modelData.url)
                            }
                        }
                        onClicked: if (!dragStarted) {
                            view.close()
                            if (modelData.openFolder) dockBackend.openFolderPath(view.folder)
                            else dockBackend.openFolderItem(modelData.url)
                        }
                    }
                }
            }
        }
        GridView {
            id: grid
            visible: view.viewMode !== "fan"
            objectName: "folderGrid"
            anchors { top: heading.bottom; bottom: view.viewMode === "list" ? listFooter.top : parent.bottom; left: parent.left; right: parent.right; leftMargin: view.viewMode === "list" ? 0 : 25; rightMargin: view.viewMode === "list" ? 0 : 25 }
            readonly property int columns: view.viewMode === "list" ? 1 : Math.max(1, Math.floor(width / 92))
            cellWidth: width / columns
            cellHeight: view.viewMode === "list" ? 24 : 98
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickDeceleration: 762
            maximumFlickVelocity: 3450
            property real wheelDestination: originY
            NumberAnimation {
                id: wheelAnimation
                objectName: "folderWheelAnimation"
                target: grid
                property: "contentY"
                duration: 378
                easing.type: Easing.OutCubic
            }
            WheelHandler {
                target: null
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: event => {
                    const delta = (event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 120 * 90) * 1.2 * 1.4 * 1.5 * 1.15 * (view.viewMode === "list" ? 2 : 1)
                    if (delta === 0) return
                    const start = wheelAnimation.running ? grid.wheelDestination : grid.contentY
                    grid.wheelDestination = Math.max(grid.originY,
                        Math.min(grid.originY + Math.max(0, grid.contentHeight - grid.height), start - delta))
                    wheelAnimation.stop()
                    wheelAnimation.from = grid.contentY
                    wheelAnimation.to = grid.wheelDestination
                    wheelAnimation.start()
                    event.accepted = true
                }
            }
            model: view.viewMode === "list" ? view.sortedEntries : view.sortedEntries.concat([{name: qsTr("Open in File Manager"), openFolder: true}])
            ScrollIndicator.vertical: ScrollIndicator {}
            delegate: Item {
                required property var modelData
                objectName: modelData.openFolder ? "folderOpenInFileManager" : "folderEntry"
                width: grid.cellWidth
                height: grid.cellHeight
                Rectangle {
                    anchors.fill: parent
                    radius: 3
                    color: "#0865ed"
                    visible: view.viewMode === "list" && mouse.containsMouse
                }
                Image {
                    objectName: "folderFileIcon"
                    x: view.viewMode === "list" ? 8 : (parent.width - width) / 2
                    y: view.viewMode === "list" ? 3 : 4; width: view.viewMode === "list" ? 18 : 52; height: view.viewMode === "list" ? 18 : 52
                    sourceSize: Qt.size(64, 64)
                    cache: false
                    source: !view.visible ? "" : modelData.openFolder ? "image://tasks/theme/exit-symbolic" : "image://tasks/fileicon/" + modelData.icon
                    visible: modelData.openFolder || thumbnail.status !== Image.Ready || thumbnail.implicitWidth <= 1
                    fillMode: Image.PreserveAspectFit
                }
                Image {
                    id: thumbnail
                    objectName: "folderThumbnail"
                    x: view.viewMode === "list" ? 8 : (parent.width - width) / 2
                    y: view.viewMode === "list" ? 3 : 4; width: view.viewMode === "list" ? 18 : 52; height: view.viewMode === "list" ? 18 : 52
                    sourceSize: Qt.size(64, 64)
                    asynchronous: true
                    cache: false
                    source: view.visible && !modelData.openFolder ? "image://folderthumbnails/" + modelData.icon : ""
                    visible: status === Image.Ready && implicitWidth > 1
                    fillMode: Image.PreserveAspectFit
                }
                Text {
                    anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: view.viewMode === "list" ? 34 : 3; rightMargin: view.viewMode === "list" ? 12 : 3; topMargin: view.viewMode === "list" ? 0 : 59; bottom: view.viewMode === "list" ? parent.bottom : undefined }
                    text: modelData.name; color: "white"
                    horizontalAlignment: view.viewMode === "list" ? Text.AlignLeft : Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight; maximumLineCount: view.viewMode === "list" ? 1 : 2; wrapMode: Text.Wrap
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: view.viewMode === "list"
                    property point pressPoint
                    property bool dragStarted: false
                    onPressed: event => { pressPoint = Qt.point(event.x, event.y); dragStarted = false }
                    onPositionChanged: event => {
                        if (modelData.openFolder || !(pressedButtons & Qt.LeftButton) || dragStarted) return
                        if (Math.hypot(event.x - pressPoint.x, event.y - pressPoint.y) >= Qt.styleHints.startDragDistance) {
                            dragStarted = true
                            dockBackend.startFolderItemDrag(view.popupWindow, modelData.url)
                        }
                    }
                    onClicked: if (!dragStarted) {
                        view.close()
                        if (modelData.openFolder) dockBackend.openFolderPath(view.folder)
                        else dockBackend.openFolderItem(modelData.url)
                    }
                }
            }
        }
        Column {
            id: listFooter
            objectName: "folderListFooter"
            visible: view.viewMode === "list"
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 50
            Rectangle { width: parent.width; height: 1; color: "#3dffffff" }
            Item {
                id: optionsRow
                objectName: "folderListOptions"
                width: parent.width
                height: 24
                Rectangle {
                    anchors.fill: parent
                    visible: optionsMouse.containsMouse || folderOptions.visible
                    gradient: Gradient {
                        GradientStop { position: 0; color: "#368dff" }
                        GradientStop { position: 0.48; color: "#0865ed" }
                        GradientStop { position: 1; color: "#034ad2" }
                    }
                }
                Text {
                    x: 25; anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Options"); color: "#f5f5f5"
                    font.family: "Inter"; font.pixelSize: 14; font.weight: Font.DemiBold
                }
                Text {
                    x: parent.width - width - 11
                    anchors.verticalCenter: parent.verticalCenter
                    text: "▶"; color: "#f5f5f5"; font.pixelSize: 10
                }
                MouseArea {
                    id: optionsMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: { if (folderOptions.visible) folderOptions.close(); else folderOptions.open() }
                }
                Item {
                    id: optionsAnchor
                    width: optionsRow.width - 5; height: optionsRow.height
                    y: view.nativeWindow ? 0 : optionsRow.height - folderOptions.height + folderOptions.bottomPadding + folderOptions.topPadding
                }
                DockMenu {
                    id: folderOptions
                    objectName: "folderListOptionsMenu"
                    parent: optionsAnchor
                    nativeWindow: view.nativeWindow
                    Component.onCompleted: {
                        // Register the submenu for popup lifetime, then anchor it
                        // to the visible footer row instead of the generated item.
                        view.addMenu(folderOptions)
                        folderOptions.parent = optionsAnchor
                    }
                    x: optionsAnchor.width
                    y: optionsRow.height - height + bottomPadding
                    DockMenuItem { text: qsTr("Sort by"); enabled: false }
                    DockMenuItem { objectName: "listSort_name"; text: qsTr("Name"); checked: view.sortMode === "name"; onTriggered: view.setPreference("sort", "name") }
                    DockMenuItem { objectName: "listSort_dateAdded"; text: qsTr("Date added"); checked: view.sortMode === "dateAdded"; onTriggered: view.setPreference("sort", "dateAdded") }
                    DockMenuItem { objectName: "listSort_dateModified"; text: qsTr("Date modified"); checked: view.sortMode === "dateModified"; onTriggered: view.setPreference("sort", "dateModified") }
                    DockMenuItem { objectName: "listSort_dateCreated"; text: qsTr("Date created"); checked: view.sortMode === "dateCreated"; onTriggered: view.setPreference("sort", "dateCreated") }
                    DockMenuItem { objectName: "listSort_type"; text: qsTr("File type"); checked: view.sortMode === "type"; onTriggered: view.setPreference("sort", "type") }
                    DockMenuSeparator {}
                    DockMenuItem { text: qsTr("View content as"); enabled: false }
                    DockMenuItem { objectName: "listViewFan"; text: qsTr("Fan"); checked: view.viewMode === "fan"; onTriggered: { view.close(); view.setPreference("view", "fan") } }
                    DockMenuItem { objectName: "listViewGrid"; text: qsTr("Grid"); checked: view.viewMode === "grid"; onTriggered: { view.close(); view.setPreference("view", "grid") } }
                    DockMenuItem { objectName: "listViewList"; text: qsTr("List"); checked: view.viewMode === "list"; onTriggered: view.setPreference("view", "list") }
                }
            }
            DockMenuItem {
                objectName: "folderListOpenInFileManager"
                width: parent.width
                height: 24
                text: qsTr("Open in File Manager")
                highlighted: hovered
                onTriggered: { view.close(); dockBackend.openFolderPath(view.folder) }
            }
        }
    }
}
