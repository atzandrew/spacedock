import QtQuick

Item {
    id: trash
    required property Item dock
    required property real shelfLip
    signal pressedChanged(bool pressed)
    signal menuRequested()
    TaskArtwork {
        id: trashArtwork
        anchors.fill: parent
        source: dockBackend.trash.full ? "qrc:/assets/icons/trash_full.png" : "qrc:/assets/icons/trash_empty.png"
        sourceSize: Qt.size(Math.ceil(tuning.artworkSize * dock.magnification.scaleSamples[0]),
                            Math.ceil(tuning.artworkSize * dock.magnification.scaleSamples[0]))
        opacity: dock.trashHover || trashClick.pressed ? 0.55 : 1
    }
    Item {
        y: trash.height
        width: trash.width
        height: dock.layout.iconBaselineInset - shelfLip
        clip: true
        opacity: dock.layout.reflectionOpacity
        TaskArtwork {
            width: trash.width; height: trash.height
            source: trashArtwork.source; sourceSize: trashArtwork.sourceSize
            mirrorVertically: true
            opacity: trashClick.pressed ? 0.55 : 1
        }
    }
    MouseArea {
        id: trashClick
        anchors.fill: parent
        onPressedChanged: trash.pressedChanged(pressed)
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton) trash.menuRequested()
            else dockBackend.openTrash()
        }
    }
}
