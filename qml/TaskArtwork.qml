import QtQuick

Item {
    id: artwork
    property url source
    property size sourceSize: Qt.size(128, 128)
    property bool placeholder: false
    property bool opened: false
    property bool mirrorVertically: false
    property int duration: 0 // owners pass tuning.motion.partingDurationMs
    property bool animateOpening: true
    property real openProgress: opened ? 1 : 0
    readonly property bool ready: image.status === Image.Ready
    Behavior on openProgress {
        enabled: artwork.animateOpening
        NumberAnimation { duration: artwork.duration; easing.type: Easing.OutCubic }
    }

    Image {
        id: image
        anchors.fill: parent
        source: artwork.placeholder ? "" : artwork.source
        sourceSize: artwork.sourceSize
        mirrorVertically: artwork.mirrorVertically
        visible: !artwork.placeholder
        smooth: true
        mipmap: true
        Component.onCompleted: if ("retainWhileLoading" in image) image.retainWhileLoading = true
    }
    Image {
        x: -artwork.width * 0.25
        y: artwork.mirrorVertically ? -artwork.height * 0.2 : -artwork.height * 0.3
        width: artwork.width * 1.5
        height: artwork.height * 1.5
        source: artwork.placeholder ? "qrc:/assets/icons/appimage_closed.png" : ""
        sourceSize: artwork.sourceSize
        visible: artwork.placeholder
        opacity: 1 - artwork.openProgress
        mirrorVertically: artwork.mirrorVertically
        smooth: true
    }
    Image {
        x: -artwork.width * 0.25
        y: artwork.mirrorVertically ? -artwork.height * 0.2 : -artwork.height * 0.3
        width: artwork.width * 1.5
        height: artwork.height * 1.5
        source: artwork.placeholder ? "qrc:/assets/icons/appimage_open.png" : ""
        sourceSize: artwork.sourceSize
        visible: artwork.placeholder
        opacity: artwork.openProgress
        mirrorVertically: artwork.mirrorVertically
        smooth: true
    }
}
