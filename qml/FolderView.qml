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
    showPointer: true
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
    pointerX: visualProgress < 1 ? openingOriginX
        : background.mapFromItem(anchor, anchor.width / 2, 0).x
    panelScale: visualProgress
    panelScaleOriginX: openingOriginX
    panelScaleOriginY: openingOriginY
    readonly property int closeDuration: Math.max(1, Math.round((tuning.autoHide.slideDurationMs ?? 400) * 0.75))
    readonly property int openDuration: Math.max(1, Math.round(closeDuration * 0.7))
    exit: Transition {
        NumberAnimation {
            target: view
            property: "closingProgress"
            to: 0.08
            duration: view.closeDuration
            easing.type: Easing.InCubic
        }
    }
    NumberAnimation {
        id: openingAnimation
        objectName: "folderOpeningAnimation"
        target: view
        property: "openingProgress"
        from: 0.08; to: 1
        duration: view.openDuration
        easing.type: Easing.OutCubic
    }
    property string folder: ""
    property var entries: []
    property bool holdsDock: false
    property bool closing: false
    function openForIcon() {
        // Popup.Item can pass the dismissing pointer release to the dock icon
        // beneath it. Treat that as the close gesture, never as a reopen.
        if (closing) return
        if (visible) { close(); return }
        openingAnimation.stop()
        closing = false
        closingProgress = 1
        openingProgress = 0.08
        folder = dockBackend.folderPath(taskItem.index)
        entries = dockBackend.folderContents(folder)
        if (!holdsDock) { holdsDock = true; dock.acquireMenuHold() }
        nativeWindow = anchor.y < height - 5
        placeAbove(anchor, dock.width)
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
        openingOriginX = pointerX
        openingAnimation.stop()
        wheelAnimation.stop()
        grid.cancelFlick()
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
    implicitWidth: Math.min(Math.max(220, dock.width - 8), 620)
    implicitHeight: Math.min(500, Math.max(110, Math.ceil((entries.length + 1) / grid.columns) * grid.cellHeight + topPadding + bottomPadding + 36))
    contentItem: Item {
        id: folderContent
        objectName: "folderContent"
        transform: Scale {
            origin.x: view.openingOriginX - folderContent.x
            origin.y: view.openingOriginY - folderContent.y
            xScale: view.visualProgress
            yScale: view.visualProgress
        }
        implicitWidth: 400
        implicitHeight: 200
        Text {
            id: heading
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
            visible: grid.contentY > grid.originY + 0.5
            z: 1
        }
        GridView {
            id: grid
            objectName: "folderGrid"
            anchors { top: heading.bottom; bottom: parent.bottom; left: parent.left; right: parent.right; leftMargin: 25; rightMargin: 25 }
            readonly property int columns: Math.max(1, Math.floor(width / 92))
            cellWidth: width / columns
            cellHeight: 98
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
                    const delta = (event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 120 * 90) * 1.2 * 1.4 * 1.5 * 1.15
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
            model: view.entries.concat([{name: qsTr("Open in File Manager"), openFolder: true}])
            ScrollIndicator.vertical: ScrollIndicator {}
            delegate: Item {
                required property var modelData
                objectName: modelData.openFolder ? "folderOpenInFileManager" : "folderEntry"
                width: grid.cellWidth
                height: grid.cellHeight
                Image {
                    objectName: "folderFileIcon"
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 4; width: 52; height: 52
                    sourceSize: Qt.size(64, 64)
                    cache: false
                    source: !view.visible ? "" : modelData.openFolder ? "image://tasks/theme/exit-symbolic" : "image://tasks/fileicon/" + modelData.icon
                    visible: modelData.openFolder || thumbnail.status !== Image.Ready || thumbnail.implicitWidth <= 1
                    fillMode: Image.PreserveAspectFit
                }
                Image {
                    id: thumbnail
                    objectName: "folderThumbnail"
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 4; width: 52; height: 52
                    sourceSize: Qt.size(64, 64)
                    asynchronous: true
                    cache: false
                    source: view.visible && !modelData.openFolder ? "image://folderthumbnails/" + modelData.icon : ""
                    visible: status === Image.Ready && implicitWidth > 1
                    fillMode: Image.PreserveAspectFit
                }
                Text {
                    anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 59; margins: 3 }
                    text: modelData.name; color: "white"; horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.Wrap
                }
                MouseArea {
                    id: mouse
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
}
