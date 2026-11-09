import QtQuick
import QtQuick.Templates as T

T.MenuSeparator {
    id: separator
    property real menuScale: 1
    // Native separators run edge to edge with roughly 2/3 of a row around them.
    implicitHeight: 14 * menuScale
    leftPadding: 2 * menuScale
    rightPadding: 2 * menuScale
    topPadding: 6 * menuScale
    bottomPadding: 7 * menuScale
    contentItem: Rectangle {
        implicitHeight: separator.menuScale
        color: "#606060"
    }
}
