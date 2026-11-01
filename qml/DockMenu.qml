import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Templates as T

T.Menu {
    id: menu
    property bool showPointer: false
    property Item pointerTarget: null
    readonly property var popupWindow: background ? background.Window.window : null
    property real pointerX: {
        // A compositor can constrain the native popup independently of the
        // requested Menu.x. Map the artwork into the actual painted panel.
        const popupX = popupWindow ? popupWindow.x : 0
        const targetX = pointerTarget ? pointerTarget.x : 0
        const targetWindowX = pointerTarget && pointerTarget.Window.window ? pointerTarget.Window.window.x : 0
        const localX = menu.x
        const opened = menu.visible
        return pointerTarget && background
            ? background.mapFromItem(pointerTarget, pointerTarget.width / 2, 0).x : width / 2
    }
    // Proportions follow the Snow Leopard/Mavericks dock menu, scaled from
    // Lucida Grande to Inter 15 by cap height.
    readonly property real pointerHeight: showPointer ? 16 : 0
    readonly property real pointerHalfWidth: 16
    readonly property real cornerRadius: 6
    // Like the native menu, the pointer sits near the left corner and the
    // menu extends to the right: tip this far from the left border.
    readonly property real pointerTipInset: 30
    readonly property real shadowMargin: 10
    // Keep the triangle on the panel while allowing its adjacent corner to
    // tighten, rather than moving the tip away from the target artwork.
    readonly property real pointerTipX: Math.max(shadowMargin + 1 + pointerHalfWidth,
        Math.min(width - shadowMargin - 1 - pointerHalfWidth, pointerX))
    // Wayland popup windows ignore negative insets. Reserve the shadow
    // inside the real window instead of painting beyond its clipped surface.
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0
    // Size to the widest row, like the native menu.
    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    // Menu-wide breathing room is independent of the blue bar's row inset.
    topPadding: 8 + shadowMargin
    bottomPadding: 8 + pointerHeight + shadowMargin
    leftPadding: 2.5 + shadowMargin
    rightPadding: 2.5 + shadowMargin
    margins: 0
    focus: true
    // Root menus: an invisible item the menu is parented to. On Wayland, Qt
    // ignores Menu.x/y and anchors the native popup's left edge to its parent
    // item's rectangle, so we move the parent to where the menu should be.
    property Item placement: null
    // Submenus are placed natively, growing up from their row's bottom-right
    // corner; see src/submenuplacement.h. Dropping that corner by the bottom
    // padding puts the submenu's last row level with the row it opened from.
    property real submenuDrop: bottomPadding
    function placeAbove(target, dockWidth) {
        // Clamp in dock coordinates, then convert into the target's coordinates.
        // The border is drawn at shadowMargin + 1 from the popup's edge.
        const center = target.x + target.width / 2
        const tipX = shadowMargin + 1 + pointerTipInset
        const left = Math.max(4, Math.min(dockWidth - width - 4, center - tipX))
        placement.x = left - target.x
        placement.width = width
        placement.height = target.height
        x = 0
        y = -height - 5 + shadowMargin
    }
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    palette.windowText: "#f5f5f5"
    delegate: DockMenuItem {
        shown: !subMenu || subMenu.enabled
    }
    Component.onCompleted: {
        if ("popupType" in menu) menu.popupType = T.Popup.Window
    }
    contentItem: ListView {
        implicitHeight: contentHeight
        implicitWidth: {
            let widest = 0
            for (let i = 0; i < menu.count; ++i) {
                const item = menu.itemAt(i)
                if (item && item.shown !== false) widest = Math.max(widest, item.implicitWidth)
            }
            return Math.ceil(widest)
        }
        model: menu.contentModel
        currentIndex: menu.currentIndex
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        ScrollIndicator.vertical: ScrollIndicator {}
    }
    background: Canvas {
        id: panel
        objectName: "dockMenuPanel"
        // Lets native placement code find the menu from its window.
        readonly property var dockMenu: menu
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: menu
            function onPointerXChanged() { panel.requestPaint() }
            function onShowPointerChanged() { panel.requestPaint() }
        }
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const pad = menu.shadowMargin
            const l = pad + 1, t = pad + 1, r = width - pad - 1, b = height - pad - menu.pointerHeight - 1
            const radius = menu.cornerRadius
            const pointerHalfWidth = menu.pointerHalfWidth
            const tip = menu.pointerTipX
            const bottomLeftRadius = menu.showPointer
                ? Math.min(radius, Math.max(0, tip - pointerHalfWidth - l)) : radius
            const bottomRightRadius = menu.showPointer
                ? Math.min(radius, Math.max(0, r - tip - pointerHalfWidth)) : radius
            ctx.beginPath()
            ctx.moveTo(l + radius, t)
            ctx.lineTo(r - radius, t)
            ctx.quadraticCurveTo(r, t, r, t + radius)
            ctx.lineTo(r, b - bottomRightRadius)
            ctx.quadraticCurveTo(r, b, r - bottomRightRadius, b)
            if (menu.showPointer) {
                ctx.lineTo(tip + pointerHalfWidth, b)
                ctx.lineTo(tip, height - pad - 1)
                ctx.lineTo(tip - pointerHalfWidth, b)
            }
            ctx.lineTo(l + bottomLeftRadius, b)
            ctx.quadraticCurveTo(l, b, l, b - bottomLeftRadius)
            ctx.lineTo(l, t + radius)
            ctx.quadraticCurveTo(l, t, l + radius, t)
            ctx.closePath()
            // A tight, soft perimeter shadow, including the pointer. Its
            // outer edge sits four pixels beyond the 2px border and remains
            // comfortably inside the popup's transparent shadow margin.
            ctx.fillStyle = "black"
            ctx.fill()
            const shadowWidths = [10, 8, 6, 4]
            const shadowAlphas = [0.04, 0.08, 0.14, 0.24]
            for (let i = 0; i < shadowWidths.length; ++i) {
                ctx.strokeStyle = "rgba(0, 0, 0, " + shadowAlphas[i] + ")"
                ctx.lineWidth = shadowWidths[i]
                ctx.lineJoin = "round"
                ctx.stroke()
            }
            // Remove the interior shadow so the translucent fill keeps its
            // own opacity rather than accumulating black underneath it.
            ctx.globalCompositeOperation = "destination-out"
            ctx.fill()
            ctx.globalCompositeOperation = "source-over"
            ctx.fillStyle = "rgba(0, 0, 0, 0.76)"
            ctx.fill()
            ctx.strokeStyle = "rgba(255, 255, 255, 0.4)"
            ctx.lineWidth = 2
            ctx.stroke()
        }
    }
}
