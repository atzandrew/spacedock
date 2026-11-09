import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: window
    required property var dock
    objectName: "dockSettings"
    title: qsTr("Dock Settings")
    width: 480
    height: Math.max(510, settingsContent.implicitHeight + 48)
    minimumWidth: 380
    minimumHeight: Math.max(300, settingsContent.implicitHeight + 48)
    color: palette.window
    SystemPalette { id: palette }
    ColumnLayout {
        id: settingsContent
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        GridLayout {
            columns: 2
            columnSpacing: 20
            rowSpacing: 14
            Layout.fillWidth: true

            Label { text: qsTr("Size") }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Slider {
                    id: sizeSlider
                    objectName: "dockSizeSlider"
                    Layout.fillWidth: true
                    from: 18; to: Math.max(from, dock.maximumIconSize)
                    property bool defaultLatched: false
                    Binding on value {
                        value: Math.min(tuning.size, sizeSlider.to)
                        when: !sizeSlider.pressed
                    }
                    onPressedChanged: {
                        if (pressed) tuning.beginSizeDrag()
                        else { defaultLatched = false; tuning.finishSizeAnimation() }
                    }
                    onMoved: {
                        if (!pressed) tuning.beginSizeDrag()
                        if (pressed && tuning.defaultSize <= to) {
                            const distance = Math.abs(value - tuning.defaultSize)
                            if (defaultLatched && distance > 5) defaultLatched = false
                            else if (!defaultLatched && distance <= 2.5) defaultLatched = true
                            if (defaultLatched) value = tuning.defaultSize
                        }
                        tuning.setDragSize(value)
                        if (!pressed) tuning.finishSizeAnimation()
                    }
                    Rectangle {
                        visible: tuning.defaultSize <= sizeSlider.to && sizeSlider.to > sizeSlider.from
                        width: 2; height: 6; radius: 1
                        x: sizeSlider.leftPadding + sizeSlider.handle.width / 2 + (sizeSlider.availableWidth - sizeSlider.handle.width) * (tuning.defaultSize - sizeSlider.from) / Math.max(1, sizeSlider.to - sizeSlider.from) - width / 2
                        y: sizeSlider.height - height
                        color: palette.text
                        opacity: 0.65
                    }
                }
                Item {
                    Layout.fillWidth: true
                    implicitHeight: minLabel.implicitHeight
                    Label { id: minLabel; text: qsTr("Minimum"); anchors.left: parent.left; font.pixelSize: 11 }
                    Label { text: qsTr("Maximum"); anchors.right: parent.right; font.pixelSize: 11 }
                    Label {
                        text: qsTr("Default")
                        visible: tuning.defaultSize <= sizeSlider.to && sizeSlider.to > sizeSlider.from
                        font.pixelSize: 11
                        x: Math.max(minLabel.width + 4, Math.min(parent.width - width - 56,
                            sizeSlider.leftPadding + sizeSlider.handle.width / 2 + (sizeSlider.availableWidth - sizeSlider.handle.width) * (tuning.defaultSize - sizeSlider.from) / Math.max(1, sizeSlider.to - sizeSlider.from) - width / 2))
                    }
                }
                Label {
                    text: qsTr("Sized to fit the screen")
                    visible: dock.layout.iconSize < tuning.size - 0.5
                    font.pixelSize: 11
                    opacity: 0.7
                }
            }
            Label { text: qsTr("Magnification") }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                CheckBox {
                    objectName: "magnificationSetting"
                    text: qsTr("Magnify icons on hover")
                    checked: tuning.magnification.enabled
                    onClicked: tuning.setMagnificationEnabled(checked)
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("%1×").arg(tuning.magnificationScale.toFixed(2))
                    enabled: tuning.magnification.enabled
                    font.pixelSize: 11
                }
                Slider {
                    id: magnificationSlider
                    objectName: "magnificationScaleSlider"
                    Layout.fillWidth: true
                    from: 1; to: 4
                    enabled: tuning.magnification.enabled
                    value: tuning.magnificationScale
                    onMoved: tuning.setMagnificationScale(Math.round(value * 20) / 20)
                    Accessible.name: qsTr("Magnification scale")
                }
                Item {
                    Layout.fillWidth: true
                    implicitHeight: scaleMin.implicitHeight
                    enabled: magnificationSlider.enabled
                    Label { id: scaleMin; text: qsTr("1×"); anchors.left: parent.left; font.pixelSize: 11 }
                    Label {
                        text: qsTr("2×")
                        font.pixelSize: 11
                        x: magnificationSlider.leftPadding + magnificationSlider.handle.width / 2
                            + (magnificationSlider.availableWidth - magnificationSlider.handle.width) / 3 - width / 2
                    }
                    Label { text: qsTr("4×"); anchors.right: parent.right; font.pixelSize: 11 }
                }
                Label {
                    objectName: "magnificationUnavailable"
                    text: qsTr("Magnification needs more screen space at this setting.")
                    visible: tuning.magnification.enabled && !dock.magnificationFits
                    font.pixelSize: 11
                    opacity: 0.7
                }
            }
            Label { text: qsTr("Position on screen"); enabled: false }
            ComboBox { enabled: false; model: [qsTr("Bottom")]; Layout.fillWidth: true }
            Label { text: qsTr("Recent applications"); enabled: false }
            CheckBox { text: qsTr("Show recent applications in Dock"); enabled: false }
            Label { text: qsTr("Startup") }
            ColumnLayout {
                CheckBox {
                    objectName: "startAtLoginSetting"
                    text: qsTr("Start at login")
                    enabled: typeof loginSettings !== "undefined" && loginSettings.available
                    checked: typeof loginSettings !== "undefined" && loginSettings.enabled
                    onClicked: loginSettings.setEnabled(checked)
                }
                Label {
                    visible: typeof loginSettings !== "undefined" && loginSettings.error.length > 0
                    text: typeof loginSettings !== "undefined" ? loginSettings.error : ""
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: "#c03030"
                }
            }
            Label { text: qsTr("Auto-hide") }
            CheckBox {
                objectName: "autoHideSetting"
                text: qsTr("Automatically hide the Dock")
                checked: dock.autoHideEnabled
                onClicked: tuning.setAutoHideEnabled(checked)
            }
            Label { text: qsTr("Click an open app") }
            ComboBox {
                objectName: "windowClickSetting"
                Layout.fillWidth: true
                model: [qsTr("Cycle through windows"), qsTr("Bring all windows forward")]
                currentIndex: tuning.bringAllWindows ? 1 : 0
                onActivated: tuning.setBringAllWindows(currentIndex === 1)
            }

        }
        Item { Layout.fillHeight: true }
    }
}
