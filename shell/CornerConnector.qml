import QtQuick
import QtQuick.Window

Window {
    id: connector

    property string corner: "top-left"
    property color backgroundColor: "#151218"
    property int radius: 7
    readonly property bool onLeft: corner.endsWith("left")
    readonly property bool onTop: corner.startsWith("top")

    visible: true
    color: "transparent"
    title: "VXWM Corner Connector " + corner
    flags: Qt.Tool
         | Qt.FramelessWindowHint
         | Qt.BypassWindowManagerHint
         | Qt.WindowStaysOnTopHint
         | Qt.WindowDoesNotAcceptFocus
         | Qt.WindowTransparentForInput

    x: Screen.virtualX + (onLeft ? 40 : Screen.width - 4 - radius)
    y: Screen.virtualY + (onTop ? 4 : Screen.height - 4 - radius)
    width: radius
    height: radius

    CornerShape {
        anchors.fill: parent
        corner: connector.corner
        fillColor: connector.backgroundColor
        radius: connector.radius
    }
}
