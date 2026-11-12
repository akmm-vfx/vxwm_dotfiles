import QtQuick
import QtQuick.Window

Window {
    id: clock

    property color textColor: "#d8bafb"
    property date currentTime: new Date()

    visible: true
    color: "transparent"
    title: "VXWM Desktop Clock"
    flags: Qt.BypassWindowManagerHint
         | Qt.FramelessWindowHint
         | Qt.WindowDoesNotAcceptFocus
         | Qt.WindowTransparentForInput
         | Qt.WindowStaysOnBottomHint

    width: label.implicitWidth + 24
    height: label.implicitHeight + 12
    x: Screen.virtualX + Screen.width - width - 16
    y: Screen.virtualY + Screen.height - height - 16

    WidgetText {
        id: label
        anchors.centerIn: parent
        text: Qt.formatDateTime(clock.currentTime, "MMM d, yyyy h:mm AP")
        textColor: clock.textColor
    }

    Timer {
        interval: 30000
        repeat: true
        running: true
        onTriggered: clock.currentTime = new Date()
    }
}
