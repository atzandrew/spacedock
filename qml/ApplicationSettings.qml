import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.iconthemes as KDEIconThemes

Window {
    id: window
    objectName: "applicationSettings"
    title: qsTr("Edit Application")
    width: 480; height: 580
    minimumWidth: 380; minimumHeight: 540
    color: palette.window
    SystemPalette { id: palette }
    property var app: ({})
    property url selectedImage: ""
    property string selectedThemeIcon: ""
    property bool restoring: false
    property string errorText: ""
    function edit(row) {
        const info = dockBackend.applicationEditorInfo(row)
        if (!info.key) return
        app = info
        selectedImage = ""
        selectedThemeIcon = ""
        restoring = false
        errorText = ""
        nameField.text = info.name
        show(); raise(); requestActivate()
    }
    readonly property url defaultSource: app.key ? "image://tasks/default/" + dockBackend.revision + "/" + app.row : ""
    readonly property url currentSource: selectedImage.toString() !== "" ? selectedImage
        : selectedThemeIcon !== "" ? "image://tasks/theme/" + encodeURIComponent(selectedThemeIcon)
        : restoring ? defaultSource : app.key ? "image://tasks/" + dockBackend.revision + "/" + app.row : ""
    onClosing: { dockBackend.imagePicker.cancel(); iconPicker.close() }
    KDEIconThemes.IconDialog {
        id: iconPicker
        objectName: "systemIconPicker"
        title: qsTr("Choose an icon")
        modality: Qt.ApplicationModal
        onAccepted: {
            if (!iconName) return
            if (iconName.startsWith("/")) {
                window.selectedImage = "file://" + encodeURI(iconName)
                window.selectedThemeIcon = ""
            } else {
                window.selectedThemeIcon = iconName
                window.selectedImage = ""
            }
            window.restoring = false
            window.errorText = ""
        }
    }
    Connections {
        target: dockBackend.imagePicker
        function onSelected(url) { window.selectedImage = url; window.selectedThemeIcon = ""; window.restoring = false; window.errorText = "" }
        function onFailed() { window.errorText = qsTr("Could not open the system file picker.") }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 14
        Label { text: qsTr("Default icon") }
        Item {
            Layout.preferredWidth: 104; Layout.preferredHeight: 104
            Image { anchors.centerIn: parent; width: 80; height: 80; source: window.defaultSource; fillMode: Image.PreserveAspectFit; sourceSize: Qt.size(128, 128) }
            Button {
                anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Restore icon"); visible: defaultHover.hovered || hovered
                onClicked: { window.selectedImage = ""; window.selectedThemeIcon = ""; window.restoring = true }
            }
            HoverHandler { id: defaultHover }
        }
        Label { text: qsTr("Icon") }
        Item {
            Layout.preferredWidth: 104; Layout.preferredHeight: 104
            Image { anchors.centerIn: parent; width: 80; height: 80; source: window.currentSource; fillMode: Image.PreserveAspectFit; sourceSize: Qt.size(128, 128) }
            Button {
                id: changeIcon
                anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Change icon"); visible: iconHover.hovered || hovered || iconMenu.visible
                onClicked: iconMenu.open()
                Menu {
                    id: iconMenu
                    MenuItem { text: qsTr("Choose Icon…"); onTriggered: iconPicker.open() }
                    MenuItem { text: qsTr("Choose Image…"); onTriggered: dockBackend.imagePicker.open() }
                }
            }
            HoverHandler { id: iconHover }
        }
        Label { text: qsTr("Name") }
        TextField { id: nameField; objectName: "applicationNameField"; Layout.fillWidth: true; selectByMouse: true }
        Label {
            Layout.fillWidth: true
            text: window.app.path || qsTr("Original file unavailable")
            wrapMode: Text.WrapAnywhere; elide: Text.ElideMiddle; maximumLineCount: 2
            font.pixelSize: 11; opacity: 0.7
        }
        Label { text: window.errorText; color: "#c33"; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            Button { text: qsTr("Cancel"); onClicked: window.close() }
            Button {
                objectName: "saveApplicationSettings"
                text: qsTr("Save"); enabled: nameField.text.trim().length > 0
                onClicked: {
                    if (dockBackend.saveApplicationEdit(window.app.key, nameField.text, window.selectedImage, window.restoring, window.selectedThemeIcon)) window.close()
                    else window.errorText = qsTr("Could not save changes. Check the image and try again.")
                }
            }
        }
    }
}
