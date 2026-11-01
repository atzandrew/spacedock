import QtQuick

Item {
    id: tooltip
    objectName: "dockToolTip"
    required property var settings
    property string label: ""
    property real targetCenter: 0
    property real targetTop: 0
    readonly property string displayedText: caption.text
    width: Math.max(0, Math.min(caption.implicitWidth + 2 * settings.padding, parent.width - 2 * settings.margin))
    height: settings.height
    x: Math.max(settings.margin, Math.min(parent.width - width - settings.margin, targetCenter - width / 2))
    // Anchor the text line, excluding the transparent space reserved for its shadow.
    y: Math.max(settings.margin, targetTop - settings.gap - caption.baselineOffset - metrics.descent)
    // Raster shadows are cached by text/style/size, independently of position.
    // Both hardware and software renderers only composite the resulting image.
    FontMetrics { id: metrics; font: caption.font }
    Image {
        id: shadow
        anchors.fill: parent
        asynchronous: true
        source: "image://tooltipshadows/" + encodeURIComponent(JSON.stringify({
            text: metrics.elidedText(tooltip.label, Qt.ElideRight, caption.width),
            width: width, height: height, dpr: Screen.devicePixelRatio,
            family: caption.font.family, fontSize: caption.font.pixelSize,
            weight: caption.font.weight, baseline: caption.y + caption.baselineOffset,
            blur: tooltip.settings.shadowBlur, coreBlur: tooltip.settings.shadowCoreBlur,
            offset: tooltip.settings.shadowOffset, strength: tooltip.settings.shadowStrength,
            sourceScale: tooltip.settings.shadowSourceScale
        }))
    }
    Text {
        id: caption
        objectName: "tooltipCaption"
        anchors.fill: parent
        anchors.leftMargin: tooltip.settings.padding
        anchors.rightMargin: tooltip.settings.padding
        text: tooltip.label
        textFormat: Text.PlainText
        font.family: tooltip.settings.fontFamily
        font.pixelSize: tooltip.settings.fontPixelSize
        font.weight: tooltip.settings.fontWeight
        color: "#ffffff"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        maximumLineCount: 1
    }
}
