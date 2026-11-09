import QtQuick
import QtQuick.Templates as T

T.MenuItem {
    id: control
    readonly property real menuScale: menu && menu.menuScale !== undefined ? menu.menuScale : 1
    // Row pitch and insets follow the native dock menu (see DockMenu.qml).
    implicitWidth: Math.max(120 * menuScale, leftPadding + label.implicitWidth + rightPadding
        + (subMenu ? 24 * menuScale : 0))
    implicitHeight: 19 * menuScale
    // Explicit show/hide that menus can read before they open; `visible`
    // reports false for every row while the popup is closed.
    property bool shown: true
    property real confirmationOpacity: 1
    onClicked: {
        if (!subMenu && menu) {
            menu.confirmingSelection = true
            confirmationBlink.restart()
        }
    }
    SequentialAnimation {
        id: confirmationBlink
        NumberAnimation { target: control; property: "confirmationOpacity"; to: 0.15; duration: 55 }
        NumberAnimation { target: control; property: "confirmationOpacity"; to: 1; duration: 65 }
        PauseAnimation { duration: 60 }
    }
    visible: shown
    height: shown ? implicitHeight : 0
    leftPadding: 25 * menuScale
    rightPadding: 20 * menuScale
    font.family: "Inter"
    font.pixelSize: 14 * menuScale
    font.weight: Font.DemiBold
    contentItem: Text {
        id: label
        text: control.text
        font: control.font
        color: control.enabled ? "#f5f5f5" : "#969696"
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        textFormat: Text.PlainText
    }
    indicator: Text {
        x: 8 * control.menuScale; width: 14 * control.menuScale; height: parent.height
        text: control.checked ? "✓" : ""
        color: "white"
        font.pixelSize: 12 * control.menuScale
        verticalAlignment: Text.AlignVCenter
    }
    arrow: Canvas {
        x: parent.width - width - 11 * control.menuScale
        y: (parent.height - height) / 2
        width: 7 * control.menuScale; height: 9 * control.menuScale
        visible: !!control.subMenu
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = "#f5f5f5"
            ctx.beginPath()
            ctx.moveTo(0, 0)
            ctx.lineTo(width, height / 2)
            ctx.lineTo(0, height)
            ctx.closePath()
            ctx.fill()
        }
    }
    background: Item {
        opacity: (control.highlighted && control.enabled || confirmationBlink.running ? 1 : 0) * control.confirmationOpacity
        visible: opacity > 0
        Behavior on opacity {
            enabled: !confirmationBlink.running
            NumberAnimation { duration: tuning.motion.menuHighlightFadeMs; easing.type: Easing.OutQuad }
        }
        readonly property real selectionHeight: Math.min(height, Math.ceil(label.implicitHeight) + 4 * control.menuScale)
        readonly property real selectionTop: (height - selectionHeight) / 2
        Rectangle {
            y: parent.selectionTop - 3 * control.menuScale
            width: parent.width
            height: 3 * control.menuScale
            gradient: Gradient {
                GradientStop { position: 0; color: "#00000000" }
                GradientStop { position: 1; color: "#90000000" }
            }
        }
        Rectangle {
            y: parent.selectionTop + parent.selectionHeight
            width: parent.width
            height: 3 * control.menuScale
            gradient: Gradient {
                GradientStop { position: 0; color: "#90000000" }
                GradientStop { position: 1; color: "#00000000" }
            }
        }
        Rectangle {
            y: parent.selectionTop
            width: parent.width
            height: parent.selectionHeight
            gradient: Gradient {
                GradientStop { position: 0; color: "#368dff" }
                GradientStop { position: 0.48; color: "#0865ed" }
                GradientStop { position: 1; color: "#034ad2" }
            }
        }
    }
}
