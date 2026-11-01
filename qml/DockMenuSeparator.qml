import QtQuick
import QtQuick.Templates as T

T.MenuSeparator {
    // Native separators run edge to edge with roughly 2/3 of a row around them.
    implicitHeight: 14
    leftPadding: 2
    rightPadding: 2
    topPadding: 6
    bottomPadding: 7
    contentItem: Rectangle {
        implicitHeight: 1
        color: "#606060"
    }
}
